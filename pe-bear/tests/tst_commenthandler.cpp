/*
 * Covers the pre-existing CommentHandler -- the store behind PE-bear's tags,
 * and the ".tag" files they are persisted to.
 *
 * Tags are user-authored annotations that can represent a lot of manual work,
 * so the properties worth pinning down are that a save/load cycle returns
 * exactly what went in, that a malformed file is skipped over rather than
 * aborting the load, and that clearing a tag removes it instead of storing an
 * empty one.
 */
#include <QtTest>
#include "base/CommentHandler.h"

namespace {

	QString writeFile(QTemporaryDir &dir, const QString &name, const QString &content)
	{
		const QString path = QDir(dir.path()).absoluteFilePath(name);
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return QString();
		QTextStream out(&f);
		out << content;
		f.close();
		return path;
	}

	QString readFile(const QString &path)
	{
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
		const QString content = QString::fromUtf8(f.readAll());
		f.close();
		return content;
	}

	/** loadFromFile is asynchronous; this waits for the load to land. */
	bool loadAndWait(CommentHandler &handler, const QString &path)
	{
		QSignalSpy updated(&handler, SIGNAL(commentsUpdated()));
		if (!handler.loadFromFile(path)) return false;
		return updated.wait(5000);
	}

}; // namespace

class TestCommentHandler : public QObject
{
	Q_OBJECT

private slots:
	void storesAndReturnsAComment();
	void returnsEmptyForAnUnknownOffset();
	void replacesACommentAtTheSameOffset();
	void anEmptyCommentRemovesTheTag();
	void anEmptyCommentIsNeverStored();
	void countsComments();
	void emitsOnChange();
	void savesAndLoadsARoundTrip();
	void savesOffsetsAsHex();
	void trimsWhitespaceOnSave();
	void keepsACommentContainingTheDelimiter();
	void skipsMalformedLinesWhileLoading();
	void loadingReplacesWhatWasThereBefore();
	void refusesToLoadAMissingFile();
	void savingNothingCreatesNoFile();
};

void TestCommentHandler::storesAndReturnsAComment()
{
	CommentHandler handler;
	handler.setComment(0x1000, "entry point");
	QCOMPARE(handler.getCommentAt(0x1000), QString("entry point"));
}

void TestCommentHandler::returnsEmptyForAnUnknownOffset()
{
	CommentHandler handler;
	QCOMPARE(handler.getCommentAt(0x4000), QString());

	handler.setComment(0x1000, "something");
	QCOMPARE(handler.getCommentAt(0x1001), QString());
}

void TestCommentHandler::replacesACommentAtTheSameOffset()
{
	CommentHandler handler;
	handler.setComment(0x1000, "first");
	handler.setComment(0x1000, "second");

	QCOMPARE(handler.getCommentAt(0x1000), QString("second"));
	QCOMPARE(int(handler.commentsNum()), 1);
}

void TestCommentHandler::anEmptyCommentRemovesTheTag()
{
	CommentHandler handler;
	handler.setComment(0x1000, "to be removed");
	QCOMPARE(int(handler.commentsNum()), 1);

	handler.setComment(0x1000, "");
	QCOMPARE(int(handler.commentsNum()), 0);
	QCOMPARE(handler.getCommentAt(0x1000), QString());
}

void TestCommentHandler::anEmptyCommentIsNeverStored()
{
	CommentHandler handler;
	handler.setComment(0x1000, "");
	QCOMPARE(int(handler.commentsNum()), 0);
}

void TestCommentHandler::countsComments()
{
	CommentHandler handler;
	QCOMPARE(int(handler.commentsNum()), 0);
	QCOMPARE(int(handler.getCommentsNum()), 0);

	handler.setComment(0x10, "a");
	handler.setComment(0x20, "b");
	handler.setComment(0x30, "c");

	QCOMPARE(int(handler.commentsNum()), 3);
	QCOMPARE(int(handler.getCommentsNum()), 3);
}

void TestCommentHandler::emitsOnChange()
{
	/* The views redraw off this signal, so it has to fire on every edit --
	   including the one that deletes a tag. */
	CommentHandler handler;
	QSignalSpy updated(&handler, SIGNAL(commentsUpdated()));

	handler.setComment(0x10, "a");
	QCOMPARE(updated.count(), 1);

	handler.setComment(0x10, "b");
	QCOMPARE(updated.count(), 2);

	handler.setComment(0x10, "");
	QCOMPARE(updated.count(), 3);
}

void TestCommentHandler::savesAndLoadsARoundTrip()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("tags.txt");

	CommentHandler writer;
	writer.setComment(0x1000, "entry point");
	writer.setComment(0x2040, "suspicious call");
	writer.setComment(0xDEADBEEF, "high offset");
	QVERIFY(writer.saveToFile(path));
	QVERIFY(QFile::exists(path));

	CommentHandler reader;
	QVERIFY(loadAndWait(reader, path));

	QCOMPARE(int(reader.commentsNum()), 3);
	QCOMPARE(reader.getCommentAt(0x1000), QString("entry point"));
	QCOMPARE(reader.getCommentAt(0x2040), QString("suspicious call"));
	QCOMPARE(reader.getCommentAt(0xDEADBEEF), QString("high offset"));
}

void TestCommentHandler::savesOffsetsAsHex()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("tags.txt");

	CommentHandler handler;
	handler.setComment(0x2A, "forty-two");
	QVERIFY(handler.saveToFile(path));

	const QString content = readFile(path);
	QVERIFY2(content.contains(QLatin1String("2a;")),
		qPrintable(QString("offset not written as hex: ") + content));
	QVERIFY(content.contains(QLatin1String("forty-two")));
}

void TestCommentHandler::trimsWhitespaceOnSave()
{
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("tags.txt");

	CommentHandler writer;
	writer.setComment(0x10, "   padded comment   ");
	QVERIFY(writer.saveToFile(path));

	CommentHandler reader;
	QVERIFY(loadAndWait(reader, path));
	QCOMPARE(reader.getCommentAt(0x10), QString("padded comment"));
}

void TestCommentHandler::keepsACommentContainingTheDelimiter()
{
	/* Only the first ';' separates the offset from the text, so a semicolon
	   inside the comment survives the round trip. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("tags.txt");

	CommentHandler writer;
	writer.setComment(0x10, "push eax; call sub_401000");
	QVERIFY(writer.saveToFile(path));

	CommentHandler reader;
	QVERIFY(loadAndWait(reader, path));
	QCOMPARE(reader.getCommentAt(0x10), QString("push eax; call sub_401000"));
}

void TestCommentHandler::skipsMalformedLinesWhileLoading()
{
	/* A hand-edited or truncated tag file must not cost the user the tags that
	   are still readable. */
	QTemporaryDir dir;
	const QString path = writeFile(dir, "mixed.txt",
		QLatin1String(
			"1000;good one\n"
			"no delimiter on this line\n"
			"zzzz;offset is not hex\n"
			"\n"
			";empty offset\n"
			"2000;good two\n"
		));
	QVERIFY(!path.isEmpty());

	CommentHandler handler;
	QVERIFY(loadAndWait(handler, path));

	QCOMPARE(handler.getCommentAt(0x1000), QString("good one"));
	QCOMPARE(handler.getCommentAt(0x2000), QString("good two"));
	QCOMPARE(int(handler.commentsNum()), 2);
}

void TestCommentHandler::loadingReplacesWhatWasThereBefore()
{
	QTemporaryDir dir;
	const QString path = writeFile(dir, "tags.txt", QLatin1String("2000;from file\n"));
	QVERIFY(!path.isEmpty());

	CommentHandler handler;
	handler.setComment(0x1000, "set by hand");
	QVERIFY(loadAndWait(handler, path));

	QCOMPARE(handler.getCommentAt(0x2000), QString("from file"));
	QVERIFY2(handler.getCommentAt(0x1000).isEmpty(),
		"loading a file left earlier tags behind");
	QCOMPARE(int(handler.commentsNum()), 1);
}

void TestCommentHandler::refusesToLoadAMissingFile()
{
	QTemporaryDir dir;
	CommentHandler handler;
	QVERIFY(!handler.loadFromFile(QDir(dir.path()).absoluteFilePath("absent.txt")));
}

void TestCommentHandler::savingNothingCreatesNoFile()
{
	/* Deliberate: an untouched file should not litter the directory with an
	   empty .tag next to it. */
	QTemporaryDir dir;
	const QString path = QDir(dir.path()).absoluteFilePath("empty.txt");

	CommentHandler handler;
	QVERIFY2(handler.saveToFile(path), "saving an empty set reported failure");
	QVERIFY2(!QFile::exists(path), "an empty tag file was created");
}

QTEST_GUILESS_MAIN(TestCommentHandler)
#include "tst_commenthandler.moc"
