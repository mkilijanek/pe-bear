/*
 * Covers the PDB path shown by the CodeView view (DebugRDSIEntryTreeModel).
 *
 * The path is meant to be NUL-terminated, but nothing in the file guarantees
 * it. The debug data ends where the debug directory's SizeOfData says, and the
 * view must stop there: whatever follows in the file (or past the end of the
 * buffer, for a mapped file without padding) is not part of the name.
 */
#include <QtTest>
#include "base/PeHandler.h"
#include "gui/pe_models/DebugTreeModel.h"
#include "MinimalPe.h"

namespace {

	void put32(QByteArray &image, int offset, quint32 value)
	{
		qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(image.data() + offset));
	}

	/* the first section maps RVA 0x1000 to raw 0x200 */
	const int DEBUG_DIR_RAW = 0x300;
	const int CV_RAW = 0x340;

	/**
	 * A PE32 with one CodeView (RSDS) debug entry whose PDB name is pdbName,
	 * written without a terminator, and with trailing written right after the
	 * debug data in the file.
	 */
	QByteArray buildWithPdb(const QByteArray &pdbName, const QByteArray &trailing)
	{
		QByteArray image = minimal_pe::build32();
		const int dataDirOffset = minimal_pe::OPTIONAL_HEADER_OFFSET + 0x60;
		put32(image, dataDirOffset + 6 * 8, 0x1000 + (DEBUG_DIR_RAW - 0x200)); /* DEBUG dir RVA */
		put32(image, dataDirOffset + 6 * 8 + 4, 28);                         /* one entry */

		const quint32 cvSize = quint32(0x18 + pdbName.size());
		put32(image, DEBUG_DIR_RAW + 0x0C, 2);                               /* Type: CODEVIEW */
		put32(image, DEBUG_DIR_RAW + 0x10, cvSize);                          /* SizeOfData */
		put32(image, DEBUG_DIR_RAW + 0x14, 0x1000 + (CV_RAW - 0x200));       /* AddressOfRawData */
		put32(image, DEBUG_DIR_RAW + 0x18, CV_RAW);                          /* PointerToRawData */

		memcpy(image.data() + CV_RAW, "RSDS", 4);
		put32(image, CV_RAW + 0x14, 1);                                      /* age */
		memcpy(image.data() + CV_RAW + 0x18, pdbName.constData(), size_t(pdbName.size()));
		memcpy(image.data() + CV_RAW + cvSize, trailing.constData(), size_t(trailing.size()));
		return image;
	}

	/** The image in a temporary file, wrapped in a PeHandler (which owns the PE and buffer). */
	class ImageFixture
	{
	public:
		explicit ImageFixture(const QByteArray &image)
			: m_handler(NULL)
		{
			if (!m_dir.isValid()) return;
			QString path = QDir(m_dir.path()).absoluteFilePath(QLatin1String("debug.exe"));
			QFile out(path);
			if (!out.open(QIODevice::WriteOnly) || out.write(image) != image.size()) return;
			out.close();
			FileBuffer *buffer = new FileBuffer(path, 0x200, true);
			m_handler = new PeHandler(new PEFile(buffer), buffer);
		}

		~ImageFixture() { if (m_handler) m_handler->release(); }

		PeHandler* handler() { return m_handler; }

	private:
		QTemporaryDir m_dir;
		PeHandler *m_handler;
	};

	QString shownPdb(PeHandler *handler)
	{
		DebugRDSIEntryTreeModel model(handler);
		model.setParentId(0);
		const QModelIndex index = model.index(DebugDirCVEntryWrapper::F_CVDBG_PDB, 2 /* VALUE */, QModelIndex());
		return model.data(index, Qt::DisplayRole).toString();
	}

} // namespace

class TestDebugModel : public QObject
{
	Q_OBJECT

private slots:
	void terminatedPdbName_isShownWhole()
	{
		ImageFixture fixture(buildWithPdb(QByteArray("app.pdb\0", 8), "XYZ"));
		QVERIFY(fixture.handler() != NULL);
		QCOMPARE(shownPdb(fixture.handler()), QString("app.pdb"));
	}

	void unterminatedPdbName_stopsAtTheEndOfTheDebugData()
	{
		ImageFixture fixture(buildWithPdb("ABCDEFGH", "XYZ"));
		QVERIFY(fixture.handler() != NULL);
		QCOMPARE(shownPdb(fixture.handler()), QString("ABCDEFGH"));
	}
};

QTEST_MAIN(TestDebugModel)
#include "tst_debugmodel.moc"
