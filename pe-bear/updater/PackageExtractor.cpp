#include "PackageExtractor.h"

using namespace pe_bear::updater;

PackageExtractor::PackageExtractor(IFileSystem *fs, IArchiveReader *reader,
	const ExtractionPolicy &policy)
	: m_fs(fs), m_reader(reader), m_policy(policy),
	m_rejection(ExtractionPolicy::NotRejected)
{
}

bool PackageExtractor::fail(const QString &why)
{
	m_lastError = why;
	return false;
}

bool PackageExtractor::extract(const QString &packagePath, const QString &destDir)
{
	m_ops.clear();
	m_rejection = ExtractionPolicy::NotRejected;
	m_lastError.clear();

	if (!m_fs || !m_reader) return fail(QLatin1String("not wired up"));
	if (packagePath.isEmpty()) return fail(QLatin1String("no package"));
	if (destDir.isEmpty()) return fail(QLatin1String("no destination"));
	/* Extracting over an existing tree would leave steps that cannot be
	   cleanly undone: the undo could not tell what it had created. */
	if (m_fs->exists(destDir)) {
		return fail(QLatin1String("the destination already exists"));
	}

	if (!m_reader->open(packagePath)) {
		return fail(QLatin1String("could not open the package: ") + m_reader->lastError());
	}
	const QList<ArchiveEntry> entries = m_reader->entries();

	/* Judged in full before a single byte is written. */
	const ExtractionPolicy::Verdict verdict = m_policy.check(entries);
	if (!verdict.ok) {
		m_reader->close();
		m_rejection = verdict.rejection;
		return fail(verdict.message());
	}

	if (!m_fs->makeDir(destDir)) {
		m_reader->close();
		return fail(QLatin1String("could not create the destination: ") + m_fs->lastError());
	}
	m_ops.append(TransactionOp(TransactionOp::OpCreatedDir, destDir));
	m_fs->restrictToOwner(destDir);

	QList<ArchiveEntry>::const_iterator itr;
	for (itr = verdict.accepted.begin(); itr != verdict.accepted.end(); ++itr) {
		/* Paths here are the policy's normalised forms, not the stored ones. */
		const QString target = QDir::cleanPath(destDir + QLatin1Char('/') + itr->path);

		/* Belt and braces against a normalisation slip: the destination must
		   still contain the result. Cheap, and the consequence of being wrong
		   is a write outside the installation. */
		const QString destPrefix = QDir::cleanPath(destDir) + QLatin1Char('/');
		if (!target.startsWith(destPrefix)) {
			m_reader->close();
			m_rejection = ExtractionPolicy::PathTraversal;
			return fail(ExtractionPolicy::rejectionMessage(
				ExtractionPolicy::PathTraversal, itr->path));
		}

		if (itr->kind == ArchiveEntry::KindDir) {
			if (!m_fs->makeDir(target)) {
				m_reader->close();
				return fail(QLatin1String("could not create ") + itr->path
					+ QLatin1String(": ") + m_fs->lastError());
			}
			m_ops.append(TransactionOp(TransactionOp::OpCreatedDir, target));
			continue;
		}

		/* A file's parent may not be listed as its own entry; archives often
		   omit directory entries entirely. */
		const QString parent = QFileInfo(target).path();
		if (!parent.isEmpty() && !m_fs->exists(parent)) {
			if (!m_fs->makeDir(parent)) {
				m_reader->close();
				return fail(QLatin1String("could not create ") + parent
					+ QLatin1String(": ") + m_fs->lastError());
			}
			m_ops.append(TransactionOp(TransactionOp::OpCreatedDir, parent));
		}

		const QByteArray data = m_reader->readEntry(itr->path);
		/* The size was judged against the policy from the header; a body that
		   disagrees with its own header means the archive is lying. */
		if (data.size() != itr->uncompressedSize) {
			m_reader->close();
			return fail(QLatin1String("the stored size of ") + itr->path
				+ QLatin1String(" does not match its contents"));
		}
		if (!m_fs->writeFile(target, data)) {
			m_reader->close();
			return fail(QLatin1String("could not write ") + itr->path
				+ QLatin1String(": ") + m_fs->lastError());
		}
		m_ops.append(TransactionOp(TransactionOp::OpCreated, target));
	}

	m_reader->close();
	return true;
}
