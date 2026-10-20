/*
 * Malformed resource directories must not make the parsed tree explode.
 *
 * hasherezade/pe-bear#77: a resource directory whose entries point back at
 * itself makes the tree grow until memory runs out, because every visit
 * expands the same directory again. bearparser bounds this by expanding each
 * directory once per tree and capping the number of entries
 * (ResourceDirWrapper::markExpanded / takeEntry).
 *
 * The symptom of a missing guard is a hang, which a test can only catch as a
 * timeout. So each image here also carries one data entry, and the test
 * counts the leaves bearparser hands to the resources album: without the
 * guard every repeated expansion adds the leaf again (121 and 1024 leaves for
 * the two shapes below), with it the leaf is reached once.
 */
#include <QtTest>
#include "MinimalPe.h"

namespace {

	/* the resource directory sits at the start of the only section */
	const quint32 RSRC_RVA = 0x1000;
	const int RSRC_RAW = 0x200;
	const int RSRC_MAX = 0x200; /* the section's SizeOfRawData */

	/* IMAGE_DIRECTORY_ENTRY_RESOURCE (2) in a PE32 optional header */
	const int RSRC_DATA_DIR = minimal_pe::OPTIONAL_HEADER_OFFSET + 0x60 + 2 * 8;

	const quint32 IS_DIR = 0x80000000;

	const int DIR_HDR_SIZE = 16;
	const int ENTRY_SIZE = 8;

	void put32(QByteArray &image, int offset, quint32 value)
	{
		qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(image.data() + offset));
	}

	/** Writes an IMAGE_RESOURCE_DIRECTORY with ID entries pointing at the
	    given offsets (relative to the resource directory). Returns its size. */
	int putDir(QByteArray &image, int rsrcOffset, const QList<quint32> &targets)
	{
		const int at = RSRC_RAW + rsrcOffset;
		/* NumberOfIdEntries */
		qToLittleEndian<quint16>(quint16(targets.size()), reinterpret_cast<uchar*>(image.data() + at + 14));
		for (int i = 0; i < targets.size(); i++) {
			put32(image, at + DIR_HDR_SIZE + i * ENTRY_SIZE, quint32(i + 1)); /* Id */
			put32(image, at + DIR_HDR_SIZE + i * ENTRY_SIZE + 4, targets.at(i));
		}
		return DIR_HDR_SIZE + targets.size() * ENTRY_SIZE;
	}

	/** Writes an IMAGE_RESOURCE_DATA_ENTRY whose 4 data bytes follow it. */
	int putDataEntry(QByteArray &image, int rsrcOffset)
	{
		const int at = RSRC_RAW + rsrcOffset;
		put32(image, at + 0, RSRC_RVA + quint32(rsrcOffset) + 16); /* OffsetToData */
		put32(image, at + 4, 4);                                   /* Size */
		return 16 + 4;
	}

	void setResourceDir(QByteArray &image, int size)
	{
		put32(image, RSRC_DATA_DIR, RSRC_RVA);
		put32(image, RSRC_DATA_DIR + 4, quint32(size));
	}

	/** Leaves bearparser registered in the resources album, over all top entries. */
	size_t leafCount(PEFile *pe, size_t topEntries)
	{
		ResourcesAlbum *album = pe->getResourcesAlbum();
		if (!album) return 0;
		size_t total = 0;
		for (size_t i = 0; i < topEntries; i++) {
			total += album->entriesCountAt(long(i));
		}
		return total;
	}

} // namespace

class TestResourceLoop : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void wellFormedTree_keepsEveryLeaf();
	void selfReferencingRoot_isExpandedOnce();
	void chainOfSharedChildren_isExpandedOnce();
	void wideSelfReference_loadsInBoundedTime();
};

/* type -> name -> language -> data, twice: the guards must not drop anything */
void TestResourceLoop::wellFormedTree_keepsEveryLeaf()
{
	QByteArray image = minimal_pe::build32();
	const int root = 0, name0 = 0x20, name1 = 0x38, lang0 = 0x50, lang1 = 0x68, data0 = 0x80, data1 = 0x98;
	putDir(image, root, QList<quint32>() << (IS_DIR | name0) << (IS_DIR | name1));
	putDir(image, name0, QList<quint32>() << (IS_DIR | lang0));
	putDir(image, name1, QList<quint32>() << (IS_DIR | lang1));
	putDir(image, lang0, QList<quint32>() << quint32(data0));
	putDir(image, lang1, QList<quint32>() << quint32(data1));
	putDataEntry(image, data0);
	const int size = data1 + putDataEntry(image, data1);
	QVERIFY(size <= RSRC_MAX);
	setResourceDir(image, size);

	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());
	PEFile pe(&buf);
	QCOMPARE(leafCount(&pe, 2), size_t(2));
}

/* hasherezade/pe-bear#77: entries of the top directory point back at it */
void TestResourceLoop::selfReferencingRoot_isExpandedOnce()
{
	QByteArray image = minimal_pe::build32();
	const int entries = 4;
	const int dataAt = DIR_HDR_SIZE + entries * ENTRY_SIZE;
	QList<quint32> targets;
	targets << quint32(dataAt);
	for (int i = 1; i < entries; i++) targets << (IS_DIR | 0);
	putDir(image, 0, targets);
	setResourceDir(image, dataAt + putDataEntry(image, dataAt));

	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());
	PEFile pe(&buf);
	/* unguarded: 1 + 3 + 9 + 27 + 81 = 121, one per visit of the top directory */
	QCOMPARE(leafCount(&pe, entries), size_t(1));
}

/* distinct directories, every entry of each pointing at the next one */
void TestResourceLoop::chainOfSharedChildren_isExpandedOnce()
{
	QByteArray image = minimal_pe::build32();
	const int entries = 4;
	const int levels = 5;
	const int dirSize = DIR_HDR_SIZE + entries * ENTRY_SIZE;
	const int dataAt = levels * dirSize;
	for (int k = 0; k < levels; k++) {
		const quint32 next = (k == levels - 1) ? quint32(dataAt) : (IS_DIR | quint32((k + 1) * dirSize));
		QList<quint32> targets;
		for (int i = 0; i < entries; i++) targets << next;
		putDir(image, k * dirSize, targets);
	}
	const int size = dataAt + putDataEntry(image, dataAt);
	QVERIFY(size <= RSRC_MAX);
	setResourceDir(image, size);

	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());
	PEFile pe(&buf);
	/* each directory is expanded under the first entry that reaches it, so the
	   last one contributes its 4 leaves once; unguarded it is 4^5 = 1024 */
	QCOMPARE(leafCount(&pe, entries), size_t(entries));
}

/* the shape of the #77 report: as many self references as fit in the section.
   Unguarded this is 61^4 expansions and does not return. */
void TestResourceLoop::wideSelfReference_loadsInBoundedTime()
{
	QByteArray image = minimal_pe::build32();
	const int entries = (RSRC_MAX - DIR_HDR_SIZE) / ENTRY_SIZE;
	QList<quint32> targets;
	for (int i = 0; i < entries; i++) targets << (IS_DIR | 0);
	setResourceDir(image, putDir(image, 0, targets));

	QElapsedTimer timer;
	timer.start();
	ByteBuffer buf((BYTE*)image.data(), (bufsize_t)image.size());
	PEFile pe(&buf);
	QCOMPARE(leafCount(&pe, size_t(entries)), size_t(0));
	/* generous for a sanitizer build on a slow runner; the guarded parse takes milliseconds */
	QVERIFY2(timer.elapsed() < 10000, qPrintable(QString("took %1 ms").arg(timer.elapsed())));
}

QTEST_MAIN(TestResourceLoop)
#include "tst_resourceloop.moc"
