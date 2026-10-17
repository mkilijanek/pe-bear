/*
 * Covers the file names "Dump all sections" and "Export disasm" build from
 * section names.
 *
 * A section name is 8 bytes the author of the PE chose. Used as it is, "/../a"
 * writes outside the chosen directory, ':' lands in an NTFS alternate data
 * stream, and two sections called the same overwrite each other's dump.
 */
#include <QtTest>
#include "base/DumpFileName.h"

class TestDumpFileName : public QObject
{
	Q_OBJECT

private slots:
	void sanitize_keepsOrdinaryNames()
	{
		QCOMPARE(DumpFileName::sanitize(".text"), QString(".text"));
		QCOMPARE(DumpFileName::sanitize("UPX0"), QString("UPX0"));
		QCOMPARE(DumpFileName::sanitize("_rel-oc.1"), QString("_rel-oc.1"));
		QCOMPARE(DumpFileName::sanitize(""), QString(""));
	}

	void sanitize_replacesPathAndStreamCharacters()
	{
		QCOMPARE(DumpFileName::sanitize("/../../a"), QString("_.._.._a"));
		QCOMPARE(DumpFileName::sanitize("..\\..\\a"), QString(".._.._a"));
		QCOMPARE(DumpFileName::sanitize("a:b"), QString("a_b"));
		QCOMPARE(DumpFileName::sanitize("c:"), QString("c_"));
		QCOMPARE(DumpFileName::sanitize("a b*?<>|\""), QString("a_b______"));
	}

	void sanitize_replacesControlAndNonAsciiCharacters()
	{
		QCOMPARE(DumpFileName::sanitize(QString::fromLatin1("a\tb\x01")), QString("a_b_"));
		QCOMPARE(DumpFileName::sanitize(QString::fromUtf8("\xC5\xBC\xC3\xB3\xC5\x82w")), QString("___w"));
		QCOMPARE(DumpFileName::sanitize(QString(QChar(0))), QString("_"));
	}

	void forSection_isConfinedToTheDirectory()
	{
		const QString name = DumpFileName::forSection("sample.exe", 2, "/../../a");
		QCOMPARE(name, QString("sample.exe[2__.._.._a]"));
		QVERIFY(!name.contains('/'));
		QVERIFY(!name.contains('\\'));
		QVERIFY(!name.contains(':'));
	}

	void forSection_sameNameDifferentSections_differ()
	{
		QVERIFY(DumpFileName::forSection("sample.exe", 0, ".text")
			!= DumpFileName::forSection("sample.exe", 1, ".text"));
		QCOMPARE(DumpFileName::forSection("sample.exe", 0, ".text"), QString("sample.exe[0_.text]"));
	}
};

QTEST_MAIN(TestDumpFileName)
#include "tst_dumpfilename.moc"
