/*
 * Covers the pre-existing StringsCollection -- the store behind the strings
 * view, filled from a worker thread and read from the GUI thread.
 */
#include <QtTest>
#include "base/StringsCollection.h"

class TestStringsCollection : public QObject
{
	Q_OBJECT

private slots:
	void storesAndReturnsStrings();
	void returnsEmptyForAnUnknownOffset();
	void tracksWhichStringsAreWide();
	void reportsByteSizeNotCharacterCount();
	void sizeOfAWideStringCountsTwoBytesPerCharacter_data();
	void sizeOfAWideStringCountsTwoBytesPerCharacter();
	void replacesAnEntryAtTheSameOffset();
	void offsetsComeBackSorted();
	void clearRemovesEverythingIncludingWideFlags();
	void fillCopiesContentAndWideFlags();
	void fillReplacesPreviousContent();
	void fillStringsLoadsFromAMap();
	void savesToFile();
	void savesSimplifiedContent();
	void reportsFailureForAnUnwritablePath();
};

void TestStringsCollection::storesAndReturnsStrings()
{
	StringsCollection strings;
	strings.insert(0x100, "kernel32.dll", false);
	strings.insert(0x200, "wide string", true);

	QCOMPARE(strings.getString(0x100), QString("kernel32.dll"));
	QCOMPARE(strings.getString(0x200), QString("wide string"));
	QCOMPARE(int(strings.size()), 2);
}

void TestStringsCollection::returnsEmptyForAnUnknownOffset()
{
	StringsCollection strings;
	strings.insert(0x100, "present", false);

	QCOMPARE(strings.getString(0x999), QString());
	QCOMPARE(int(strings.getStringSize(0x999)), 0);
	QVERIFY(!strings.isWide(0x999));
}

void TestStringsCollection::tracksWhichStringsAreWide()
{
	StringsCollection strings;
	strings.insert(0x100, "ansi", false);
	strings.insert(0x200, "wide", true);

	QVERIFY(!strings.isWide(0x100));
	QVERIFY(strings.isWide(0x200));
}

void TestStringsCollection::reportsByteSizeNotCharacterCount()
{
	/* The strings view uses this to highlight the right byte range, so a wide
	   string has to report twice its character count. */
	StringsCollection strings;
	strings.insert(0x100, "abcd", false);
	strings.insert(0x200, "abcd", true);

	QCOMPARE(int(strings.getStringSize(0x100)), 4);
	QCOMPARE(int(strings.getStringSize(0x200)), 8);
}

void TestStringsCollection::sizeOfAWideStringCountsTwoBytesPerCharacter_data()
{
	QTest::addColumn<QString>("text");
	QTest::addColumn<bool>("isWide");
	QTest::addColumn<int>("expected");

	QTest::newRow("empty ansi") << "" << false << 0;
	QTest::newRow("empty wide") << "" << true << 0;
	QTest::newRow("one char ansi") << "a" << false << 1;
	QTest::newRow("one char wide") << "a" << true << 2;
	QTest::newRow("long ansi") << "kernel32.dll" << false << 12;
	QTest::newRow("long wide") << "kernel32.dll" << true << 24;
}

void TestStringsCollection::sizeOfAWideStringCountsTwoBytesPerCharacter()
{
	QFETCH(QString, text);
	QFETCH(bool, isWide);
	QFETCH(int, expected);

	QCOMPARE(int(util::getStringSize(text, isWide)), expected);
}

void TestStringsCollection::replacesAnEntryAtTheSameOffset()
{
	StringsCollection strings;
	strings.insert(0x100, "first", false);
	strings.insert(0x100, "second", false);

	QCOMPARE(strings.getString(0x100), QString("second"));
	QCOMPARE(int(strings.size()), 1);
}

void TestStringsCollection::offsetsComeBackSorted()
{
	/* The view lists strings in file order, which relies on this. */
	StringsCollection strings;
	strings.insert(0x300, "third", false);
	strings.insert(0x100, "first", false);
	strings.insert(0x200, "second", false);

	const QList<offset_t> offsets = strings.getOffsets();
	QCOMPARE(offsets.size(), 3);
	QCOMPARE(quint64(offsets.at(0)), quint64(0x100));
	QCOMPARE(quint64(offsets.at(1)), quint64(0x200));
	QCOMPARE(quint64(offsets.at(2)), quint64(0x300));
}

void TestStringsCollection::clearRemovesEverythingIncludingWideFlags()
{
	StringsCollection strings;
	strings.insert(0x100, "ansi", false);
	strings.insert(0x200, "wide", true);

	strings.clear();

	QCOMPARE(int(strings.size()), 0);
	QVERIFY(strings.getOffsets().isEmpty());
	/* a stale wide flag would misreport the size of a later string here */
	QVERIFY2(!strings.isWide(0x200), "a wide flag survived clear()");
}

void TestStringsCollection::fillCopiesContentAndWideFlags()
{
	StringsCollection source;
	source.insert(0x100, "ansi", false);
	source.insert(0x200, "wide", true);

	StringsCollection target;
	target.fill(source);

	QCOMPARE(int(target.size()), 2);
	QCOMPARE(target.getString(0x100), QString("ansi"));
	QCOMPARE(target.getString(0x200), QString("wide"));
	QVERIFY(!target.isWide(0x100));
	QVERIFY2(target.isWide(0x200), "fill() lost the wide flag");
}

void TestStringsCollection::fillReplacesPreviousContent()
{
	StringsCollection source;
	source.insert(0x200, "from source", false);

	StringsCollection target;
	target.insert(0x100, "pre-existing", false);
	target.fill(source);

	QCOMPARE(int(target.size()), 1);
	QCOMPARE(target.getString(0x200), QString("from source"));
	QVERIFY(target.getString(0x100).isEmpty());
}

void TestStringsCollection::fillStringsLoadsFromAMap()
{
	QMap<offset_t, QString> map;
	map.insert(0x100, "one");
	map.insert(0x200, "two");

	StringsCollection strings;
	strings.insert(0x999, "will be replaced", false);
	QVERIFY(strings.fillStrings(&map));

	QCOMPARE(int(strings.size()), 2);
	QCOMPARE(strings.getString(0x100), QString("one"));
	QVERIFY(strings.getString(0x999).isEmpty());

	QVERIFY2(!strings.fillStrings(NULL), "a null map was accepted");
}

void TestStringsCollection::savesToFile()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("strings.txt");

	StringsCollection strings;
	strings.insert(0x2A, "kernel32.dll", false);
	QVERIFY(strings.saveToFile(path));

	QFile f(path);
	QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString content = QString::fromUtf8(f.readAll());
	f.close();

	QVERIFY2(content.contains(QLatin1String("2a;kernel32.dll")),
		qPrintable(QString("unexpected content: ") + content));
}

void TestStringsCollection::savesSimplifiedContent()
{
	/* Embedded newlines and runs of whitespace would break the one-per-line
	   format, so the content is simplified on the way out. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("strings.txt");

	StringsCollection strings;
	strings.insert(0x10, "has\nnewline  and   spaces", false);
	QVERIFY(strings.saveToFile(path));

	QFile f(path);
	QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
	const QString content = QString::fromUtf8(f.readAll()).trimmed();
	f.close();

	QCOMPARE(content.count(QLatin1Char('\n')), 0);
	QVERIFY(content.contains(QLatin1String("has newline and spaces")));
}

void TestStringsCollection::reportsFailureForAnUnwritablePath()
{
	StringsCollection strings;
	strings.insert(0x10, "content", false);
	QVERIFY(!strings.saveToFile(QLatin1String("/this/path/does/not/exist/out.txt")));
}

QTEST_GUILESS_MAIN(TestStringsCollection)
#include "tst_stringscollection.moc"
