#include "ayu/data/media_dumper.h"

#include "ayu/features/watcher/target_watcher.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace AyuMediaDumper {

namespace {

QString sanitizeName(QString name) {
	static const QRegularExpression regex("[\\\\/:*?\"<>|]");
	return name.replace(regex, "_").trimmed();
}

} // namespace

QString getUserDirectory(HistoryItem *item) {
	if (!item) {
		return QString();
	}

	const auto from = item->from();
	const auto userId = from ? (from->id.value & PeerId::kChatTypeMask) : 0;
	QString userTag;

	if (const auto user = from ? from->asUser() : nullptr) {
		const auto username = user->username();
		if (!username.isEmpty()) {
			userTag = QString("%1_%2").arg(userId).arg(username);
		} else {
			const auto name = sanitizeName(user->name());
			userTag = name.isEmpty() ? QString::number(userId) : QString("%1_%2").arg(userId).arg(name);
		}
	} else if (from) {
		userTag = QString::number(userId);
	} else {
		userTag = "unknown_peer";
	}

	const QString baseDir = cWorkingDir() + "saved_deleted/" + sanitizeName(userTag) + "/";
	QDir().mkpath(baseDir);
	return baseDir;
}

void dumpDeletedMessage(HistoryItem *item) {
	if (!item) return;

	// Also log into target watcher if user is on the watchlist
	AyuTargetWatcher::logDeletedMessage(item);

	const QString dir = getUserDirectory(item);
	if (dir.isEmpty()) return;

	// 1. Text log of deleted message
	const QString text = item->originalText().text;
	const auto dateStr = QDateTime::fromSecsSinceEpoch(item->date()).toString("yyyy-MM-dd HH:mm:ss");
	const auto chatTitle = item->history()->peer ? item->history()->peer->name() : "Private Chat";

	QFile logFile(dir + "deleted_messages.txt");
	if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream out(&logFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("[%1] [MsgID: %2] [Chat: %3]\n")
			       .arg(dateStr)
			       .arg(item->id.bare)
			       .arg(chatTitle);
		if (!text.isEmpty()) {
			out << text << "\n";
		}
		out << "----------------------------------------\n";
	}

	// 2. Dump media files if present
	if (const auto media = item->media()) {
		if (const auto photo = media->photo()) {
			if (const auto mediaView = photo->activeMediaView()) {
				const auto path = dir + QString("photo_%1_%2.jpg").arg(item->id.bare).arg(item->date());
				mediaView->saveToFile(path);
			}
		} else if (const auto document = media->document()) {
			const auto filePath = document->filepath(true);
			if (!filePath.isEmpty() && QFile::exists(filePath)) {
				const auto destPath = dir + QString("doc_%1_%2")
					.arg(item->id.bare)
					.arg(sanitizeName(document->filename()));
				QFile::copy(filePath, destPath);
			}
		}
	}
}

void dumpTtlMedia(HistoryItem *item) {
	if (!item) return;

	const auto media = item->media();
	if (!media) return;

	const auto from = item->from();
	const auto userId = from ? (from->id.value & PeerId::kChatTypeMask) : 0;

	const QString dir = getUserDirectory(item);
	if (dir.isEmpty()) return;

	if (const auto photo = media->photo()) {
		const auto photoMedia = photo->createMediaView();
		if (photoMedia) {
			const auto path = dir + QString("ttl_photo_%1_%2.jpg").arg(item->id.bare).arg(item->date());
			photoMedia->saveToFile(path);

			if (AyuTargetWatcher::isWatched(userId)) {
				const QString targetDir = AyuTargetWatcher::getTargetDirectory(userId) + "ttl/";
				QDir().mkpath(targetDir);
				photoMedia->saveToFile(targetDir + QString("ttl_photo_%1_%2.jpg").arg(item->id.bare).arg(item->date()));
			}
		}
	} else if (const auto document = media->document()) {
		const auto filePath = document->filepath(true);
		if (!filePath.isEmpty() && QFile::exists(filePath)) {
			const auto destPath = dir + QString("ttl_video_%1_%2.mp4").arg(item->id.bare).arg(item->date());
			QFile::copy(filePath, destPath);

			if (AyuTargetWatcher::isWatched(userId)) {
				const QString targetDir = AyuTargetWatcher::getTargetDirectory(userId) + "ttl/";
				QDir().mkpath(targetDir);
				QFile::copy(filePath, targetDir + QString("ttl_video_%1_%2.mp4").arg(item->id.bare).arg(item->date()));
			}
		}
	}
}

} // namespace AyuMediaDumper
