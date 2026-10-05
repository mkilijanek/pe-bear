#include <QtTest>
#include "../HelperRelocation.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

/*
 * The decision and the copying are tested against a fake disk; the one piece
 * that looks at the real process, loadedModules(), is tested for real on
 * whatever platform the suite runs on, because that is the part that differs
 * per platform and the part a fake would only agree with itself about.
 */
class TestHelperRelocation : public QObject
{
	Q_OBJECT
private slots:
	void notNeededWhenTheHelperRunsFromElsewhere();
	void notFooledByATargetThatIsAPrefixOfAnotherName();
	void neededWhenTheHelperIsInsideTheTarget();
	void copiesOnlyTheModulesUnderTheTargetAndKeepsTheirLayout();
	void theExecutableIsCopiedEvenWhenNotReportedAsAModule();
	void withoutARunIdThePlanIsNeededButHasNowhereToGo();
	void carryOutPutsTheFilesInPlaceAndMarksTheExecutable();
	void carryOutRemovesAHalfCopyWhenACopyFails();
	void carryOutReplacesACrashedEarlierAttemptForTheSameRun();
	void carryOutIsANoOpWhenNotNeeded();
	void sweepRemovesOtherRunsAndKeepsThisOne();
	void sweepOnAMissingRootIsNothing();
	void loadedModulesOfThisProcessIncludeQtCore();

private:
	static QStringList modules()
	{
		return QStringList()
			<< "C:/apps/pe-bear/pe-bear-updater.exe"
			<< "C:/Windows/System32/KERNEL32.DLL"
			<< "C:/apps/pe-bear/Qt6Core.dll"
			<< "C:/apps/pe-bear/plugins/tls/qschannelbackend.dll"
			<< "C:/apps/pe-bear-old/archive.dll"
			<< "C:/Windows/System32/ucrtbase.dll";
	}
};

void TestHelperRelocation::notNeededWhenTheHelperRunsFromElsewhere()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/Users/me/AppData/Local/PE-bear/PE-bear-updates/helper/abc/pe-bear-updater.exe",
		modules(), "C:/apps/pe-bear", "C:/Users/me/AppData/Local/PE-bear/PE-bear-updates/helper", "abc");
	QVERIFY(!p.needed);
	QVERIFY(p.copies.isEmpty());
	QVERIFY(p.destExe.isEmpty());
}

void TestHelperRelocation::notFooledByATargetThatIsAPrefixOfAnotherName()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear-old/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QVERIFY(!p.needed);
}

void TestHelperRelocation::neededWhenTheHelperIsInsideTheTarget()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QVERIFY(p.needed);
	QCOMPARE(p.destDir, QString("C:/h/abc"));
	QCOMPARE(p.destExe, QString("C:/h/abc/pe-bear-updater.exe"));
}

void TestHelperRelocation::copiesOnlyTheModulesUnderTheTargetAndKeepsTheirLayout()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QStringList from, to;
	for (int i = 0; i < p.copies.size(); i++) {
		from << p.copies.at(i).from;
		to << p.copies.at(i).to;
	}
	QCOMPARE(from, QStringList()
		<< "C:/apps/pe-bear/pe-bear-updater.exe"
		<< "C:/apps/pe-bear/Qt6Core.dll"
		<< "C:/apps/pe-bear/plugins/tls/qschannelbackend.dll");
	QCOMPARE(to, QStringList()
		<< "C:/h/abc/pe-bear-updater.exe"
		<< "C:/h/abc/Qt6Core.dll"
		<< "C:/h/abc/plugins/tls/qschannelbackend.dll");
}

void TestHelperRelocation::theExecutableIsCopiedEvenWhenNotReportedAsAModule()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"/opt/pe-bear/pe-bear-updater", QStringList() << "/usr/lib/libQt6Core.so.6",
		"/opt/pe-bear", "/home/me/.local/share/PE-bear/PE-bear-updates/helper", "r1");
	QVERIFY(p.needed);
	QCOMPARE(p.copies.size(), 1);
	QCOMPARE(p.copies.first().from, QString("/opt/pe-bear/pe-bear-updater"));
	QCOMPARE(p.destExe, QString("/home/me/.local/share/PE-bear/PE-bear-updates/helper/r1/pe-bear-updater"));
}

void TestHelperRelocation::withoutARunIdThePlanIsNeededButHasNowhereToGo()
{
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", QString());
	QVERIFY(p.needed);
	QVERIFY(p.destDir.isEmpty());

	FakeFileSystem fs;
	QString why;
	QVERIFY(!HelperRelocation::carryOut(&fs, p, &why));
	QVERIFY(!why.isEmpty());
	QCOMPARE(fs.callCount("copyFile"), 0);
}

void TestHelperRelocation::carryOutPutsTheFilesInPlaceAndMarksTheExecutable()
{
	FakeFileSystem fs;
	fs.addFile("C:/apps/pe-bear/pe-bear-updater.exe", "exe");
	fs.addFile("C:/apps/pe-bear/Qt6Core.dll", "core");
	fs.addFile("C:/apps/pe-bear/plugins/tls/qschannelbackend.dll", "tls");
	fs.addDir("C:/h");

	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QString why;
	QVERIFY2(HelperRelocation::carryOut(&fs, p, &why), qPrintable(why));

	QCOMPARE(fs.contentOf("C:/h/abc/pe-bear-updater.exe"), QByteArray("exe"));
	QCOMPARE(fs.contentOf("C:/h/abc/Qt6Core.dll"), QByteArray("core"));
	QCOMPARE(fs.contentOf("C:/h/abc/plugins/tls/qschannelbackend.dll"), QByteArray("tls"));
	QVERIFY(fs.isExecutable("C:/h/abc/pe-bear-updater.exe"));
	/* The originals are untouched: the installer, not this class, moves them. */
	QVERIFY(fs.hasFile("C:/apps/pe-bear/pe-bear-updater.exe"));
	QVERIFY(fs.hasFile("C:/apps/pe-bear/Qt6Core.dll"));
}

void TestHelperRelocation::carryOutRemovesAHalfCopyWhenACopyFails()
{
	FakeFileSystem fs;
	fs.addFile("C:/apps/pe-bear/pe-bear-updater.exe", "exe");
	fs.addFile("C:/apps/pe-bear/Qt6Core.dll", "core");
	fs.addFile("C:/apps/pe-bear/plugins/tls/qschannelbackend.dll", "tls");
	fs.addDir("C:/h");
	fs.failForPath("copyFile", "C:/apps/pe-bear/Qt6Core.dll");

	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QString why;
	QVERIFY(!HelperRelocation::carryOut(&fs, p, &why));
	QVERIFY(!why.isEmpty());
	QVERIFY(!fs.hasFile("C:/h/abc/pe-bear-updater.exe"));
	QVERIFY(!fs.hasDir("C:/h/abc"));
}

void TestHelperRelocation::carryOutReplacesACrashedEarlierAttemptForTheSameRun()
{
	FakeFileSystem fs;
	fs.addFile("C:/apps/pe-bear/pe-bear-updater.exe", "exe");
	fs.addFile("C:/h/abc/pe-bear-updater.exe", "stale");
	fs.addFile("C:/h/abc/leftover.dll", "stale");

	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/apps/pe-bear/pe-bear-updater.exe", QStringList(), "C:/apps/pe-bear", "C:/h", "abc");
	QString why;
	QVERIFY2(HelperRelocation::carryOut(&fs, p, &why), qPrintable(why));
	QCOMPARE(fs.contentOf("C:/h/abc/pe-bear-updater.exe"), QByteArray("exe"));
	QVERIFY(!fs.hasFile("C:/h/abc/leftover.dll"));
}

void TestHelperRelocation::carryOutIsANoOpWhenNotNeeded()
{
	FakeFileSystem fs;
	const HelperRelocation::Plan p = HelperRelocation::plan(
		"C:/elsewhere/pe-bear-updater.exe", modules(), "C:/apps/pe-bear", "C:/h", "abc");
	QString why;
	QVERIFY(HelperRelocation::carryOut(&fs, p, &why));
	QCOMPARE(fs.callCount("makeDir"), 0);
	QCOMPARE(fs.callCount("copyFile"), 0);
}

void TestHelperRelocation::sweepRemovesOtherRunsAndKeepsThisOne()
{
	FakeFileSystem fs;
	fs.addFile("C:/h/old1/pe-bear-updater.exe");
	fs.addFile("C:/h/old2/pe-bear-updater.exe");
	fs.addFile("C:/h/old2/Qt6Core.dll");
	fs.addFile("C:/h/mine/pe-bear-updater.exe");
	fs.addFile("C:/h/not-a-dir.txt");

	QCOMPARE(HelperRelocation::sweep(&fs, "C:/h", "mine"), 2);
	QVERIFY(!fs.hasDir("C:/h/old1"));
	QVERIFY(!fs.hasDir("C:/h/old2"));
	QVERIFY(fs.hasFile("C:/h/mine/pe-bear-updater.exe"));
	QVERIFY(fs.hasFile("C:/h/not-a-dir.txt"));
}

void TestHelperRelocation::sweepOnAMissingRootIsNothing()
{
	FakeFileSystem fs;
	QCOMPARE(HelperRelocation::sweep(&fs, "C:/h", "mine"), 0);
	QCOMPARE(HelperRelocation::sweep(&fs, QString(), "mine"), 0);
	QCOMPARE(fs.callCount("removeDirRecursively"), 0);
}

void TestHelperRelocation::loadedModulesOfThisProcessIncludeQtCore()
{
	const QStringList mods = HelperRelocation::loadedModules();
	QVERIFY2(!mods.isEmpty(), "no modules reported for the running process");
	bool core = false;
	for (int i = 0; i < mods.size(); i++) {
		const QString &m = mods.at(i);
		QVERIFY2(!m.contains(QLatin1Char('\\')), qPrintable(m));
		QVERIFY2(QFileInfo(m).isAbsolute(), qPrintable(m));
		QVERIFY2(QFileInfo(m).exists(), qPrintable(m));
		if (QFileInfo(m).fileName().contains(QLatin1String("Qt"), Qt::CaseInsensitive)
			&& m.contains(QLatin1String("Core")))
		{
			core = true;
		}
	}
	QVERIFY2(core, qPrintable(mods.join(QLatin1String("\n"))));
}

QTEST_GUILESS_MAIN(TestHelperRelocation)
#include "tst_helperrelocation.moc"
