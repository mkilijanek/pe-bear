#include "TransactionJournal.h"

using namespace pe_bear::updater;

const char* TransactionJournal::FILE_SUFFIX = ".json";

TransactionJournal::TransactionJournal(IFileSystem *fs, const QString &transactionsDir)
	: m_fs(fs), m_dir(QDir::cleanPath(transactionsDir))
{
}

bool TransactionJournal::prepare()
{
	if (!m_fs) {
		m_lastError = QLatin1String("no filesystem");
		return false;
	}
	if (m_dir.isEmpty()) {
		m_lastError = QLatin1String("no transactions directory");
		return false;
	}
	if (!m_fs->makeDir(m_dir)) {
		m_lastError = m_fs->lastError();
		return false;
	}
	m_fs->restrictToOwner(m_dir);
	return true;
}

QString TransactionJournal::pathFor(const QString &id) const
{
	if (id.isEmpty()) return QString();
	/* The id becomes a file name, so anything that could leave the directory
	   is refused rather than sanitised -- a silently rewritten id would make
	   the record unfindable later. */
	if (id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\'))
		|| id.contains(QLatin1String("..")))
	{
		return QString();
	}
	return QDir::cleanPath(m_dir + QDir::separator() + id + QLatin1String(FILE_SUFFIX));
}

bool TransactionJournal::write(TransactionRecord &record)
{
	if (!m_fs) { m_lastError = QLatin1String("no filesystem"); return false; }
	if (!record.isValid()) { m_lastError = QLatin1String("incomplete record"); return false; }

	const QString path = pathFor(record.id);
	if (path.isEmpty()) { m_lastError = QLatin1String("unusable transaction id"); return false; }

	if (record.createdAt.isEmpty()) {
		record.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	}
	record.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

	if (!m_fs->writeFile(path, record.toJson())) {
		m_lastError = m_fs->lastError();
		return false;
	}
	m_fs->restrictToOwner(path);
	return true;
}

TransactionRecord TransactionJournal::read(const QString &id, bool *ok) const
{
	if (ok) *ok = false;
	if (!m_fs) { m_lastError = QLatin1String("no filesystem"); return TransactionRecord(); }

	const QString path = pathFor(id);
	if (path.isEmpty() || !m_fs->exists(path)) {
		m_lastError = QLatin1String("no such transaction: ") + id;
		return TransactionRecord();
	}
	const QByteArray data = m_fs->readFile(path);
	if (data.isEmpty()) {
		m_lastError = QLatin1String("empty or unreadable journal: ") + id;
		return TransactionRecord();
	}
	bool parsedOk = false;
	TransactionRecord rec = TransactionRecord::fromJson(data, &parsedOk);
	if (!parsedOk) {
		m_lastError = QLatin1String("unparseable journal: ") + id;
		return rec;
	}
	if (ok) *ok = true;
	return rec;
}

QStringList TransactionJournal::listIds() const
{
	QStringList ids;
	if (!m_fs) return ids;

	const QString suffix = QLatin1String(FILE_SUFFIX);
	const QStringList entries = m_fs->listDir(m_dir);
	QStringList::const_iterator itr;
	for (itr = entries.begin(); itr != entries.end(); ++itr) {
		const QString name = *itr;
		if (!name.endsWith(suffix)) continue;
		ids << name.left(name.length() - suffix.length());
	}
	return ids;
}

QList<TransactionRecord> TransactionJournal::findUnfinished() const
{
	QList<TransactionRecord> out;
	const QStringList ids = listIds();
	QStringList::const_iterator itr;
	for (itr = ids.begin(); itr != ids.end(); ++itr) {
		bool ok = false;
		const TransactionRecord rec = read(*itr, &ok);
		if (!ok) continue; /* reported by findUnreadable instead */
		if (!isTerminalState(rec.state)) out.append(rec);
	}
	return out;
}

QStringList TransactionJournal::findUnreadable() const
{
	QStringList out;
	const QStringList ids = listIds();
	QStringList::const_iterator itr;
	for (itr = ids.begin(); itr != ids.end(); ++itr) {
		bool ok = false;
		read(*itr, &ok);
		if (!ok) out << *itr;
	}
	return out;
}

bool TransactionJournal::remove(const QString &id)
{
	if (!m_fs) { m_lastError = QLatin1String("no filesystem"); return false; }
	const QString path = pathFor(id);
	if (path.isEmpty()) { m_lastError = QLatin1String("unusable transaction id"); return false; }
	if (!m_fs->removeFile(path)) {
		m_lastError = m_fs->lastError();
		return false;
	}
	return true;
}
