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

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <mutex>

namespace AyuTargetWatcher {

namespace {

std::mutex g_watcherMutex;
std::unordered_set<uint64> g_watchedUsers;
bool g_initialized = false;

QString getConfigFile() {
	return cWorkingDir() + "tdata/watched_targets.json";
}

QString sanitize(QString name) {
	static const QRegularExpression regex("[\\\\/:*?\"<>|]");
	return name.replace(regex, "_").trimmed();
}

void loadWatchedFromFile() {
	if (g_initialized) return;
	g_initialized = true;

	QFile file(getConfigFile());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isArray()) {
		for (const auto &val : doc.array()) {
			g_watchedUsers.insert(val.toVariant().toULongLong());
		}
	}
}

void saveWatchedToFile() {
	QFile file(getConfigFile());
	QDir().mkpath(cWorkingDir() + "tdata/");
	if (file.open(QIODevice::WriteOnly)) {
		QJsonArray arr;
		for (const auto id : g_watchedUsers) {
			arr.append(qint64(id));
		}
		file.write(QJsonDocument(arr).toJson());
	}
}

} // namespace

bool isWatched(uint64 userId) {
	std::lock_guard lock(g_watcherMutex);
	loadWatchedFromFile();
	return g_watchedUsers.contains(userId);
}

void addTarget(uint64 userId, const QString &username, const QString &name) {
	{
		std::lock_guard lock(g_watcherMutex);
		loadWatchedFromFile();
		g_watchedUsers.insert(userId);
		saveWatchedToFile();
	}
	const QString dir = getTargetDirectory(userId);
	QDir().mkpath(dir + "deleted/");
	QDir().mkpath(dir + "ttl/");
	QDir().mkpath(dir + "avatars/");

	QFile infoFile(dir + "profile_history.txt");
	if (infoFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream out(&infoFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("[%1] Наблюдение начато. ID: %2, Ник: @%3, Имя: %4\n")
			.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
			.arg(userId)
			.arg(username)
			.arg(name);
	}
}

void removeTarget(uint64 userId) {
	std::lock_guard lock(g_watcherMutex);
	loadWatchedFromFile();
	g_watchedUsers.erase(userId);
	saveWatchedToFile();
}

bool toggleTarget(uint64 userId, const QString &username, const QString &name) {
	if (isWatched(userId)) {
		removeTarget(userId);
		return false;
	} else {
		addTarget(userId, username, name);
		return true;
	}
}

std::vector<uint64> getWatchedList() {
	std::lock_guard lock(g_watcherMutex);
	loadWatchedFromFile();
	return std::vector<uint64>(g_watchedUsers.begin(), g_watchedUsers.end());
}

QString getTargetDirectory(uint64 userId) {
	const QString dir = cWorkingDir() + "targets/" + QString::number(userId) + "/";
	QDir().mkpath(dir);
	return dir;
}

void logDeletedMessage(HistoryItem *item) {
	if (!item) return;
	const auto from = item->from();
	const auto userId = from ? (from->id.value & PeerId::kChatTypeMask) : 0;
	if (!isWatched(userId)) return;

	const QString dir = getTargetDirectory(userId) + "deleted/";
	QDir().mkpath(dir);

	const QString text = item->originalText().text;
	const auto dateStr = QDateTime::fromSecsSinceEpoch(item->date()).toString("yyyy-MM-dd HH:mm:ss");
	const auto chatTitle = item->history()->peer ? item->history()->peer->name() : "Личный чат";

	QFile logFile(dir + "messages.txt");
	if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream out(&logFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("[%1] [MsgID: %2] [Чат: %3]\n").arg(dateStr).arg(item->id.bare).arg(chatTitle);
		if (!text.isEmpty()) {
			out << text << "\n";
		}
		out << "----------------------------------------\n";
	}

	if (const auto media = item->media()) {
		if (const auto photo = media->photo()) {
			if (const auto mediaView = photo->createMediaView()) {
				mediaView->saveToFile(dir + QString("photo_%1_%2.jpg").arg(item->id.bare).arg(item->date()));
			}
		} else if (const auto doc = media->document()) {
			const auto fp = doc->filepath(true);
			if (!fp.isEmpty() && QFile::exists(fp)) {
				QFile::copy(fp, dir + QString("doc_%1_%2").arg(item->id.bare).arg(sanitize(doc->filename())));
			}
		}
	}
}

void logMessageEdit(HistoryItem *item, const QString &oldText, const QString &newText) {
	if (!item) return;
	const auto from = item->from();
	const auto userId = from ? (from->id.value & PeerId::kChatTypeMask) : 0;
	if (!isWatched(userId)) return;

	const QString dir = getTargetDirectory(userId);
	QFile editFile(dir + "edits.log");
	if (editFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream out(&editFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("[%1] [MsgID: %2]\n  Было: \"%3\"\n  Стало: \"%4\"\n")
			.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
			.arg(item->id.bare)
			.arg(oldText)
			.arg(newText);
		out << "----------------------------------------\n";
	}
}

void logActivity(uint64 userId, const QString &actionText) {
	if (!isWatched(userId)) return;

	const QString dir = getTargetDirectory(userId);
	QFile actFile(dir + "activity.log");
	if (actFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
		QTextStream out(&actFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("[%1] %2\n")
			.arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
			.arg(actionText);
	}
}

void backupChatBeforeDelete(uint64 userId, const QString &chatTitle, const QString &fullLog) {
	if (!isWatched(userId)) return;

	const QString dir = getTargetDirectory(userId);
	QFile backupFile(dir + QString("chat_backup_attempted_delete_%1.txt").arg(QDateTime::currentSecsSinceEpoch()));
	if (backupFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream out(&backupFile);
		out.setEncoding(QStringConverter::Utf8);
		out << QString("=== БЭКАП ЧАТА: %1 (Собеседник нажал 'Удалить у обоих') ===\n").arg(chatTitle);
		out << QString("Время попытки удаления: %1\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
		out << "------------------------------------------------------------\n";
		out << fullLog;
	}
}

} // namespace AyuTargetWatcher
