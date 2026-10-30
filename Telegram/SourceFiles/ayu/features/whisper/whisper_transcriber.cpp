#include "ayu/features/whisper/whisper_transcriber.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <whisper.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <algorithm>
#include <vector>

namespace AyuWhisper {

namespace {

const QString kModelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin";

QString getModelPath() {
	const QString dir = cWorkingDir() + "whisper/";
	QDir().mkpath(dir);
	return dir + "ggml-base.bin";
}

// Convert any audio file (.ogg, .oga, .mp4, etc.) to 16kHz Mono Float32 using bundled FFmpeg
std::vector<float> decodeAudioTo16kMono(const QString &filePath) {
	std::vector<float> output;

	AVFormatContext *formatCtx = nullptr;
	if (avformat_open_input(&formatCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0) {
		return output;
	}
	if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
		avformat_close_input(&formatCtx);
		return output;
	}

	const int audioStream = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	if (audioStream < 0) {
		avformat_close_input(&formatCtx);
		return output;
	}

	AVCodecParameters *codecParams = formatCtx->streams[audioStream]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
	if (!codec) {
		avformat_close_input(&formatCtx);
		return output;
	}

	AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx || avcodec_parameters_to_context(codecCtx, codecParams) < 0 || avcodec_open2(codecCtx, codec, nullptr) < 0) {
		if (codecCtx) avcodec_free_context(&codecCtx);
		avformat_close_input(&formatCtx);
		return output;
	}

	SwrContext *swr = swr_alloc();
	AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_MONO;
	swr_alloc_set_opts2(&swr,
		&outLayout,
		AV_SAMPLE_FMT_FLT,
		16000,
		&codecCtx->ch_layout,
		codecCtx->sample_fmt,
		codecCtx->sample_rate,
		0, nullptr);
	swr_init(swr);

	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	while (av_read_frame(formatCtx, pkt) >= 0) {
		if (pkt->stream_index == audioStream) {
			if (avcodec_send_packet(codecCtx, pkt) >= 0) {
				while (avcodec_receive_frame(codecCtx, frame) >= 0) {
					const int outSamples = av_rescale_rnd(
						swr_get_delay(swr, codecCtx->sample_rate) + frame->nb_samples,
						16000,
						codecCtx->sample_rate,
						AV_ROUND_UP);

					const int oldSize = output.size();
					output.resize(oldSize + outSamples);
					uint8_t *outBuffer = reinterpret_cast<uint8_t*>(output.data() + oldSize);

					const int converted = swr_convert(swr, &outBuffer, outSamples,
						const_cast<const uint8_t**>(frame->data), frame->nb_samples);
					if (converted > 0) {
						output.resize(oldSize + converted);
					} else {
						output.resize(oldSize);
					}
				}
			}
		}
		av_packet_unref(pkt);
	}

	av_frame_free(&frame);
	av_packet_free(&pkt);
	swr_free(&swr);
	avcodec_free_context(&codecCtx);
	avformat_close_input(&formatCtx);

	return output;
}

// Local inference execution via whisper.cpp
QString runWhisperInference(const QString &modelPath, const std::vector<float> &samples) {
	if (samples.empty()) {
		return QString();
	}

	whisper_context_params cparams = whisper_context_default_params();
	whisper_context *ctx = whisper_init_from_file_with_params(modelPath.toUtf8().constData(), cparams);
	if (!ctx) {
		return "[Whisper Error: Model init failed]";
	}

	whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	wparams.print_progress = false;
	wparams.print_special = false;
	wparams.print_realtime = false;
	wparams.print_timestamps = false;
	wparams.translate = false;
	wparams.language = "auto";
	wparams.n_threads = std::clamp(QThread::idealThreadCount(), 2, 8);

	QString fullText;
	if (whisper_full(ctx, wparams, samples.data(), samples.size()) == 0) {
		const int nSegments = whisper_full_n_segments(ctx);
		for (int i = 0; i < nSegments; ++i) {
			const char *text = whisper_full_get_segment_text(ctx, i);
			fullText += QString::fromUtf8(text);
		}
	} else {
		fullText = "[Whisper: Transcription failed]";
	}

	whisper_free(ctx);
	return fullText.trimmed();
}

} // namespace

void initialize() {
	if (!isModelReady()) {
		ensureModel([](bool success) {
			LOG(("Whisper model downloaded: %1").arg(success));
		});
	}
}

bool isModelReady() {
	return QFile::exists(getModelPath());
}

void ensureModel(std::function<void(bool success)> onDone) {
	const QString dest = getModelPath();
	if (QFile::exists(dest)) {
		if (onDone) onDone(true);
		return;
	}

	auto *nam = new QNetworkAccessManager();
	QNetworkRequest req(QUrl(kModelUrl));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

	QNetworkReply *reply = nam->get(req);
	QObject::connect(reply, &QNetworkReply::finished, [reply, nam, dest, onDone]() {
		reply->deleteLater();
		nam->deleteLater();

		if (reply->error() == QNetworkReply::NoError) {
			QFile f(dest + ".tmp");
			if (f.open(QIODevice::WriteOnly)) {
				f.write(reply->readAll());
				f.close();
				QFile::rename(dest + ".tmp", dest);
				if (onDone) onDone(true);
				return;
			}
		}
		if (onDone) onDone(false);
	});
}

void transcribe(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) return;
	const auto doc = media->document();
	if (!doc || (!doc->isVoiceMessage() && !doc->isVideoMessage())) return;

	auto &session = item->history()->session();
	const auto fullId = item->fullId();

	QString audioPath = doc->filepath(true);
	if (audioPath.isEmpty()) {
		doc->save(Data::FileOrigin(), QString());
		audioPath = doc->loadingFilePath();
	}

	ensureModel([=, &session](bool modelOk) {
		if (!modelOk) {
			return;
		}

		crl::async([=, &session]() {
			QString resolvedPath = doc->filepath(true);
			if (resolvedPath.isEmpty()) {
				return;
			}

			const auto pcm = decodeAudioTo16kMono(resolvedPath);
			const QString text = runWhisperInference(getModelPath(), pcm);

			crl::on_main(&session, [=, &session]() {
				if (const auto targetItem = session.data().message(fullId)) {
					session.api().transcribes().applyCustomResult(targetItem, text);
				}
			});
		});
	});
}

void processIncomingMessage(not_null<HistoryItem*> item) {
	if (item->out()) return;
	if (const auto media = item->media()) {
		if (const auto doc = media->document()) {
			if (doc->isVoiceMessage() || doc->isVideoMessage()) {
				transcribe(item);
			}
		}
	}
}

} // namespace AyuWhisper
