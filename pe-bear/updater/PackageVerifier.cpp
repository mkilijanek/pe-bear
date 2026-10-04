#include "PackageVerifier.h"

using namespace pe_bear::updater;

const int PackageVerifier::CHUNK_BYTES;
const int AsyncPackageVerifier::CHUNKS_PER_PASS;

QString PackageVerifier::computeSha256(const QString &path, UpdateError *error)
{
	if (error) *error = ErrorNone;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) *error = ErrorStorage;
		return QString();
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	QByteArray buffer;
	buffer.resize(CHUNK_BYTES);

	while (!file.atEnd()) {
		const qint64 read = file.read(buffer.data(), CHUNK_BYTES);
		if (read < 0) {
			file.close();
			if (error) *error = ErrorStorage;
			return QString();
		}
		if (read == 0) break;
		hash.addData(buffer.constData(), static_cast<int>(read));
	}
	file.close();
	return QString::fromLatin1(hash.result().toHex()).toLower();
}

PackageVerifier::Result PackageVerifier::verify(const QString &path, qint64 expectedSize,
	const QString &expectedSha256, bool removeOnFailure)
{
	Result result;

	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		result.error = ErrorStorage;
		return result;
	}
	result.size = info.size();

	/* Size first: it is free, and a mismatch means there is no point hashing
	   hundreds of megabytes to reach the same conclusion. */
	if (expectedSize > 0 && result.size != expectedSize) {
		result.error = ErrorSizeMismatch;
		if (removeOnFailure) result.deleted = QFile::remove(path);
		return result;
	}
	if (expectedSha256.length() != 64) {
		result.error = ErrorInvalidDigest;
		if (removeOnFailure) result.deleted = QFile::remove(path);
		return result;
	}

	UpdateError hashError = ErrorNone;
	result.sha256 = computeSha256(path, &hashError);
	if (result.sha256.isEmpty()) {
		result.error = (hashError == ErrorNone) ? ErrorStorage : hashError;
		return result;
	}
	if (result.sha256.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
		result.error = ErrorDigestMismatch;
		if (removeOnFailure) result.deleted = QFile::remove(path);
		return result;
	}
	result.ok = true;
	return result;
}

//----------------------------------------------------------------------

AsyncPackageVerifier::AsyncPackageVerifier(QObject *parent)
	: QObject(parent), m_hash(QCryptographicHash::Sha256), m_timer(this),
	m_expectedSize(0), m_hashed(0), m_removeOnFailure(true), m_busy(false)
{
	m_timer.setSingleShot(true);
	m_timer.setInterval(0);
	connect(&m_timer, SIGNAL(timeout()), this, SLOT(step()));
	m_buffer.resize(PackageVerifier::CHUNK_BYTES);
	qRegisterMetaType<pe_bear::updater::PackageVerifier::Result>(
		"pe_bear::updater::PackageVerifier::Result");
}

AsyncPackageVerifier::~AsyncPackageVerifier()
{
	reset();
}

void AsyncPackageVerifier::reset()
{
	m_timer.stop();
	if (m_file.isOpen()) {
		m_file.close();
	}
	m_hash.reset();
	m_hashed = 0;
	m_busy = false;
}

void AsyncPackageVerifier::start(const QString &path, qint64 expectedSize,
	const QString &expectedSha256, bool removeOnFailure)
{
	if (m_busy) return;

	m_path = path;
	m_expectedSize = expectedSize;
	m_expectedSha256 = expectedSha256.toLower();
	m_removeOnFailure = removeOnFailure;
	m_hashed = 0;
	m_hash.reset();

	PackageVerifier::Result result;

	const QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		result.error = ErrorStorage;
		finishWith(result);
		return;
	}
	result.size = info.size();
	if (expectedSize > 0 && result.size != expectedSize) {
		result.error = ErrorSizeMismatch;
		if (m_removeOnFailure) result.deleted = QFile::remove(path);
		finishWith(result);
		return;
	}
	if (m_expectedSha256.length() != 64) {
		result.error = ErrorInvalidDigest;
		if (m_removeOnFailure) result.deleted = QFile::remove(path);
		finishWith(result);
		return;
	}
	m_file.setFileName(path);
	if (!m_file.open(QIODevice::ReadOnly)) {
		result.error = ErrorStorage;
		finishWith(result);
		return;
	}
	m_busy = true;
	m_timer.start();
}

void AsyncPackageVerifier::cancel()
{
	if (!m_busy) return;
	reset();

	PackageVerifier::Result result;
	result.error = ErrorCancelled;
	emit finished(result);
}

void AsyncPackageVerifier::step()
{
	if (!m_busy) return;

	for (int i = 0; i < CHUNKS_PER_PASS; i++) {
		const qint64 read = m_file.read(m_buffer.data(), PackageVerifier::CHUNK_BYTES);
		if (read < 0) {
			PackageVerifier::Result result;
			result.size = m_file.size();
			result.error = ErrorStorage;
			finishWith(result);
			return;
		}
		if (read == 0) {
			PackageVerifier::Result result;
			result.size = m_hashed;
			result.sha256 = QString::fromLatin1(m_hash.result().toHex()).toLower();
			if (result.sha256 != m_expectedSha256) {
				result.error = ErrorDigestMismatch;
				if (m_removeOnFailure) {
					m_file.close();
					result.deleted = QFile::remove(m_path);
				}
			} else {
				result.ok = true;
			}
			finishWith(result);
			return;
		}
		m_hash.addData(m_buffer.constData(), static_cast<int>(read));
		m_hashed += read;
	}
	emit progress(m_hashed, m_expectedSize);
	m_timer.start();
}

void AsyncPackageVerifier::finishWith(const PackageVerifier::Result &result)
{
	reset();
	emit finished(result);
}
