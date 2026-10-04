#pragma once

#include <QtCore>
#include <bearparser/bearparser.h>
#include "base/PeHandler.h"
#include "MinimalPe.h"

/**
 * A PeHandler over a synthetic PE, for the model checks.
 *
 * PeHandler needs a file-backed FileBuffer, so the generated image is written
 * to a temporary file that lives as long as this object.
 *
 * Teardown follows the ownership the production code actually uses: PeHandler
 * is reference-counted (Releasable) with a protected destructor, and its
 * destructor deletes both the PEFile and the FileBuffer. So a live handler is
 * disposed of with release(), never delete, and the PE and buffer are only
 * freed here if the handler was never constructed. Every view increments the
 * count on construction and releases on destruction, so models must be deleted
 * before the fixture goes away.
 */
class PeFixture
{
public:
	explicit PeFixture(const minimal_pe::Options &options = minimal_pe::Options())
		: m_dir(), m_fileBuffer(NULL), m_pe(NULL), m_handler(NULL)
	{
		if (!m_dir.isValid()) return;

		const QByteArray image = minimal_pe::build(options);
		m_path = QDir(m_dir.path()).absoluteFilePath(QLatin1String("synthetic.exe"));

		QFile out(m_path);
		if (!out.open(QIODevice::WriteOnly)) return;
		const bool written = (out.write(image) == image.size());
		out.close();
		if (!written) return;

		try {
			/* the same minimum size PeHandlerFactory uses */
			const bufsize_t minSize = sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD)
				+ sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
			m_fileBuffer = new FileBuffer(m_path, minSize, true);
			m_pe = new PEFile(m_fileBuffer);
			m_handler = new PeHandler(m_pe, m_fileBuffer);
		} catch (CustomException &e) {
			m_error = e.getInfo();
			cleanup();
		}
	}

	~PeFixture() { cleanup(); }

	bool isValid() const { return m_handler != NULL; }
	QString error() const { return m_error; }

	PeHandler* handler() { return m_handler; }
	PEFile* pe() { return m_pe; }
	QString path() const { return m_path; }

private:
	void cleanup()
	{
		if (m_handler) {
			/* takes the PEFile and the FileBuffer with it */
			m_handler->release();
			m_handler = NULL;
			m_pe = NULL;
			m_fileBuffer = NULL;
			return;
		}
		delete m_pe; m_pe = NULL;
		delete m_fileBuffer; m_fileBuffer = NULL;
	}

	PeFixture(const PeFixture&);
	PeFixture& operator=(const PeFixture&);

	QTemporaryDir m_dir;
	QString m_path;
	QString m_error;
	FileBuffer *m_fileBuffer;
	PEFile *m_pe;
	PeHandler *m_handler;
};
