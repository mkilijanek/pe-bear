/*
 * Tiny PE files: smaller than the headers PE-bear reads.
 *
 * hasherezade/pe-bear#22 reported that PE-bear refused tiny but valid files
 * (the Corkami set: 97, 252, 268 bytes ...), and showed no disassembly for a
 * 64-bit one. These files fold the headers into each other: the PE header
 * starts inside the DOS header, e_lfanew doubles as SectionAlignment, there are
 * no sections, and the code lives in the headers. The file ends before the
 * optional header does; the loader reads the rest as zeros.
 *
 * PE-bear opens a file the way PeHandlerFactory::addHandler does, padding the
 * buffer up to the largest header set, so the images below go through that
 * same path and are then disassembled at the entry point, as the Disasm view
 * does when a file is opened.
 */
#include <QtTest>
#include <bearparser/bearparser.h>
#include "PeDisasm.h"

namespace {

	void put16(QByteArray &b, int at, quint16 v) { qToLittleEndian<quint16>(v, reinterpret_cast<uchar*>(b.data() + at)); }
	void put32(QByteArray &b, int at, quint32 v) { qToLittleEndian<quint32>(v, reinterpret_cast<uchar*>(b.data() + at)); }
	void put64(QByteArray &b, int at, quint64 v) { qToLittleEndian<quint64>(v, reinterpret_cast<uchar*>(b.data() + at)); }

	const int LFANEW = 4; /* also SectionAlignment: both live at 0x3C */
	const int OPT_HDR = LFANEW + 4 + 20;
	const quint32 CODE_AT = 0x80;

	/**
	 * A PE whose NT headers start at offset 4, inside the DOS header, with no
	 * sections and NumberOfRvaAndSizes = 0, cut off at fileSize bytes.
	 * The code at CODE_AT is "push 0x2a; pop eax; ret".
	 */
	QByteArray buildTiny(bool is64bit, int fileSize)
	{
		QByteArray b(0x100, '\0');
		put16(b, 0x00, 0x5A4D);                                  /* "MZ" */
		put32(b, LFANEW, 0x00004550);                            /* "PE\0\0" */
		put16(b, LFANEW + 4, is64bit ? 0x8664 : 0x014C);         /* Machine */
		put16(b, LFANEW + 4 + 16, is64bit ? 0xF0 : 0xE0);        /* SizeOfOptionalHeader */
		put16(b, LFANEW + 4 + 18, is64bit ? 0x0022 : 0x0102);    /* Characteristics */

		put16(b, OPT_HDR + 0x00, is64bit ? 0x020B : 0x010B);     /* Magic */
		put32(b, OPT_HDR + 0x10, CODE_AT);                       /* AddressOfEntryPoint */
		if (is64bit) {
			put64(b, OPT_HDR + 0x18, Q_UINT64_C(0x140000000));   /* ImageBase */
		} else {
			put32(b, OPT_HDR + 0x1C, 0x400000);                  /* ImageBase */
		}
		/* SectionAlignment and e_lfanew are the same four bytes, at 0x3C */
		put32(b, OPT_HDR + 0x20, LFANEW);                        /* SectionAlignment */
		put32(b, OPT_HDR + 0x24, LFANEW);                        /* FileAlignment */
		put16(b, OPT_HDR + 0x30, 4);                             /* MajorSubsystemVersion */
		put32(b, OPT_HDR + 0x38, 0x1000);                        /* SizeOfImage */
		put32(b, OPT_HDR + 0x3C, CODE_AT);                       /* SizeOfHeaders */
		put16(b, OPT_HDR + 0x44, 2);                             /* Subsystem: GUI */
		/* NumberOfRvaAndSizes stays 0 */

		const char code[] = { '\x6A', '\x2A', '\x58', '\xC3' };
		memcpy(b.data() + CODE_AT, code, sizeof(code));
		b.truncate(fileSize);
		return b;
	}

	/** What PeHandlerFactory::addHandler does with a file the user opens. */
	FileBuffer* openLikePeBear(QString &path)
	{
		const size_t peMinSize = sizeof(IMAGE_DOS_HEADER) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
		return new FileBuffer(path, bufsize_t(peMinSize), true);
	}

} // namespace

class TestTinyPe : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { ExeFactory::init(); }
	void cleanupTestCase() { ExeFactory::destroy(); }

	void tinyPe_loadsAndDisassemblesAtTheEntryPoint_data();
	void tinyPe_loadsAndDisassemblesAtTheEntryPoint();
};

void TestTinyPe::tinyPe_loadsAndDisassemblesAtTheEntryPoint_data()
{
	QTest::addColumn<bool>("is64bit");
	QTest::addColumn<int>("fileSize");
	QTest::addColumn<QString>("firstInsn");

	/* the file ends before the optional header does (at 0x1C + 0xE0) */
	QTest::newRow("PE32, 0x84 bytes") << false << 0x84 << QString("push 0x2a");
	QTest::newRow("PE32+, 0x84 bytes") << true << 0x84 << QString("push 0x2a");
}

void TestTinyPe::tinyPe_loadsAndDisassemblesAtTheEntryPoint()
{
	QFETCH(bool, is64bit);
	QFETCH(int, fileSize);
	QFETCH(QString, firstInsn);

	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QString path = QDir(dir.path()).absoluteFilePath(QLatin1String("tiny.exe"));
	{
		QFile out(path);
		QVERIFY(out.open(QIODevice::WriteOnly));
		const QByteArray image = buildTiny(is64bit, fileSize);
		QCOMPARE(int(out.write(image)), fileSize);
	}

	FileBuffer *fileBuffer = NULL;
	PEFile *pe = NULL;
	try {
		fileBuffer = openLikePeBear(path);
		pe = new PEFile(fileBuffer);
	} catch (CustomException &e) {
		delete fileBuffer;
		QFAIL(qPrintable(QString("refused: ") + e.what()));
	}

	QCOMPARE(pe->getBitMode(), is64bit ? 64 : 32);
	QCOMPARE(int(pe->getSectionsCount()), 0);
	QCOMPARE(quint64(pe->getEntryPoint()), quint64(CODE_AT));

	const offset_t epRaw = pe->rvaToRaw(pe->getEntryPoint());
	QCOMPARE(quint64(epRaw), quint64(CODE_AT));

	pe_bear::PeDisasm disasm(pe);
	QVERIFY(disasm.init(epRaw, pe->getArch(), (Executable::exe_bits) pe->getBitMode()));
	QVERIFY(disasm.fillTable());
	QVERIFY(disasm.chunksCount() >= 3);
	QCOMPARE(disasm.mnemStr(0), firstInsn);
	QCOMPARE(disasm.mnemStr(1), is64bit ? QString("pop rax") : QString("pop eax"));
	QCOMPARE(disasm.mnemStr(2).trimmed(), QString("ret"));

	delete pe;
	delete fileBuffer;
}

QTEST_MAIN(TestTinyPe)
#include "tst_tinype.moc"
