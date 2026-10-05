#pragma once

#include <QtCore>
#include "../FileSystem.h"

/**
 * An in-memory filesystem that can be told to fail.
 *
 * This is what makes the transaction logic testable at all. A real filesystem
 * cannot be asked to fail a rename on the third call, or to report a directory
 * as unwritable on demand -- but those are exactly the situations the journal
 * and the rollback ordering exist for. Here each can be arranged precisely and
 * reproducibly, on any host.
 *
 * Paths are kept as cleaned strings; directories are tracked separately from
 * files so that moving a tree behaves like the real thing.
 */
class FakeFileSystem : public pe_bear::updater::IFileSystem
{
public:
	FakeFileSystem() : m_writableDefault(true) {}

	/* --- arranging the world --- */

	void addDir(const QString &path)
	{
		const QString p = clean(path);
		m_dirs.insert(p);
		/* parents exist implicitly, as they do on a real filesystem */
		QString parent = parentOf(p);
		while (!parent.isEmpty() && !m_dirs.contains(parent)) {
			m_dirs.insert(parent);
			parent = parentOf(parent);
		}
	}

	void addFile(const QString &path, const QByteArray &content = QByteArray("x"))
	{
		const QString p = clean(path);
		m_files.insert(p, content);
		const QString parent = parentOf(p);
		if (!parent.isEmpty()) addDir(parent);
	}

	void setWritable(const QString &path, bool writable) { m_writable.insert(clean(path), writable); }
	void setWritableDefault(bool writable) { m_writableDefault = writable; }

	/* --- arranging failures --- */

	/** Fail every call of this kind, e.g. "movePath". */
	void failAlways(const QString &op) { m_failAlways.insert(op); }
	/** Fail the n-th call of this kind, counting from 1. */
	void failOnCall(const QString &op, int nth) { m_failNth.insert(op, nth); }
	/** Fail any call of this kind touching this exact path. */
	void failForPath(const QString &op, const QString &path)
	{
		m_failPath.insert(op, clean(path));
	}
	void clearFailures() { m_failAlways.clear(); m_failNth.clear(); m_failPath.clear(); }

	int callCount(const QString &op) const { return m_calls.value(op, 0); }

	/* --- inspecting the result --- */

	bool hasFile(const QString &path) const { return m_files.contains(clean(path)); }
	bool hasDir(const QString &path) const { return m_dirs.contains(clean(path)); }
	bool isExecutable(const QString &path) const { return m_executable.contains(clean(path)); }
	/** True once setStandardPermissions has been applied to the path. */
	bool isReadableByAll(const QString &path) const { return m_installable.contains(clean(path)); }
	QByteArray contentOf(const QString &path) const { return m_files.value(clean(path)); }
	int fileCount() const { return m_files.size(); }

	/* --- IFileSystem --- */

	virtual bool exists(const QString &path) const
	{
		const QString p = clean(path);
		return m_files.contains(p) || m_dirs.contains(p);
	}

	virtual bool isDir(const QString &path) const { return m_dirs.contains(clean(path)); }

	virtual qint64 fileSize(const QString &path) const
	{
		const QString p = clean(path);
		if (!m_files.contains(p)) return -1;
		return m_files.value(p).size();
	}

	virtual QStringList listDir(const QString &path) const
	{
		const QString prefix = clean(path) + QLatin1Char('/');
		QSet<QString> names;
		for (QMap<QString, QByteArray>::const_iterator i = m_files.begin(); i != m_files.end(); ++i) {
			if (i.key().startsWith(prefix)) {
				names.insert(i.key().mid(prefix.length()).section(QLatin1Char('/'), 0, 0));
			}
		}
		foreach (const QString &d, m_dirs) {
            if (d.startsWith(prefix)) {
                names.insert(d.mid(prefix.length()).section(QLatin1Char('/'), 0, 0));
            }
        }
		QStringList out = names.values();
		out.sort();
		return out;
	}

	virtual QString canonicalPath(const QString &path) const
	{
		const QString p = clean(path);
		return exists(p) ? p : QString();
	}

	virtual bool isWritableDir(const QString &path) const
	{
		const QString p = clean(path);
		if (!m_dirs.contains(p)) return false;
		return m_writable.value(p, m_writableDefault);
	}

	virtual bool makeDir(const QString &path)
	{
		if (shouldFail(QLatin1String("makeDir"), path)) return fail("makeDir refused");
		addDir(path);
		return true;
	}

	virtual bool removeFile(const QString &path)
	{
		if (shouldFail(QLatin1String("removeFile"), path)) return fail("removeFile refused");
		m_files.remove(clean(path));
		return true;
	}

	virtual bool removeDirRecursively(const QString &path)
	{
		if (shouldFail(QLatin1String("removeDirRecursively"), path)) return fail("removeDir refused");
		const QString p = clean(path);
		const QString prefix = p + QLatin1Char('/');
		foreach (const QString &k, m_files.keys()) {
			if (k == p || k.startsWith(prefix)) m_files.remove(k);
		}
		foreach (const QString &d, m_dirs.values()) {
			if (d == p || d.startsWith(prefix)) m_dirs.remove(d);
		}
		return true;
	}

	virtual bool movePath(const QString &from, const QString &to)
	{
		/* One check for the whole operation. Calling shouldFail twice would
		   advance the call counter twice per move, making failOnCall(n) refer
		   to something a test author cannot predict. */
		if (shouldFail2(QLatin1String("movePath"), from, to)) return fail("movePath refused");

		const QString f = clean(from);
		const QString t = clean(to);
		if (!exists(f)) return fail("movePath: source missing");
		if (exists(t)) return fail("movePath: destination exists");

		if (m_files.contains(f)) {
			m_files.insert(t, m_files.take(f));
			addDir(parentOf(t));
			return true;
		}
		/* a directory: carry the subtree across */
		const QString prefix = f + QLatin1Char('/');
		foreach (const QString &k, m_files.keys()) {
			if (k.startsWith(prefix)) m_files.insert(t + k.mid(f.length()), m_files.take(k));
		}
		foreach (const QString &d, m_dirs.values()) {
			if (d == f || d.startsWith(prefix)) {
				m_dirs.remove(d);
				m_dirs.insert(t + d.mid(f.length()));
			}
		}
		addDir(t);
		return true;
	}

	virtual bool copyFile(const QString &from, const QString &to)
	{
		if (shouldFail(QLatin1String("copyFile"), from)) return fail("copyFile refused");
		const QString f = clean(from);
		const QString t = clean(to);
		if (!m_files.contains(f)) return fail("copyFile: source missing");
		if (exists(t)) return fail("copyFile: destination exists");
		m_files.insert(t, m_files.value(f));
		addDir(parentOf(t));
		return true;
	}

	virtual bool writeFile(const QString &path, const QByteArray &data)
	{
		if (shouldFail(QLatin1String("writeFile"), path)) return fail("writeFile refused");
		addFile(path, data);
		return true;
	}

	virtual QByteArray readFile(const QString &path) const
	{
		const QString p = clean(path);
		/* counted so a test can fail the n-th read */
		m_calls[QLatin1String("readFile")]++;
		if (m_failAlways.contains(QLatin1String("readFile"))) return QByteArray();
		return m_files.value(p);
	}

	virtual bool setStandardPermissions(const QString &path, bool executable)
	{
		const QString p = clean(path);
		if (shouldFail(QLatin1String("setStandardPermissions"), p)) return false;
		if (!m_files.contains(p)) return fail("setStandardPermissions: no such file");
		m_installable.insert(p);
		if (executable) m_executable.insert(p); else m_executable.remove(p);
		return true;
	}
	virtual bool restrictToOwner(const QString &) { return true; }

	virtual QString lastError() const { return m_lastError; }

private:
	static QString clean(const QString &p) { return QDir::cleanPath(p); }

	static QString parentOf(const QString &p)
	{
		const int slash = p.lastIndexOf(QLatin1Char('/'));
		if (slash <= 0) return QString();
		return p.left(slash);
	}

	bool fail(const char *why) const { m_lastError = QLatin1String(why); return false; }

	/** Fails if either path matches, counting the operation once. */
	bool shouldFail2(const QString &op, const QString &a, const QString &b) const
	{
		const int n = ++m_calls[op];
		if (m_failAlways.contains(op)) return true;
		if (m_failNth.value(op, -1) == n) return true;
		if (m_failPath.contains(op)) {
			foreach (const QString &p, m_failPath.values(op)) {
				if (p == clean(a) || p == clean(b)) return true;
			}
		}
		return false;
	}

	bool shouldFail(const QString &op, const QString &path) const
	{
		const int n = ++m_calls[op];
		if (m_failAlways.contains(op)) return true;
		if (m_failNth.value(op, -1) == n) return true;
		if (m_failPath.contains(op)) {
			foreach (const QString &p, m_failPath.values(op)) {
				if (p == clean(path)) return true;
			}
		}
		return false;
	}

	QMap<QString, QByteArray> m_files;
	QSet<QString> m_dirs;
	QMap<QString, bool> m_writable;
	bool m_writableDefault;
	QSet<QString> m_executable;
	QSet<QString> m_installable;

	QSet<QString> m_failAlways;
	QMap<QString, int> m_failNth;
	QMultiMap<QString, QString> m_failPath;
	mutable QMap<QString, int> m_calls;
	mutable QString m_lastError;
};
