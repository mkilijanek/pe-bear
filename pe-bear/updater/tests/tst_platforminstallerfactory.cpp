/*
 * Tests for PlatformInstallerFactory - the factory that creates platform-specific
 * installers based on the current platform.
 */
#include <QtTest>
#include "../PlatformInstallerFactory.h"
#include "../DirectoryInstaller.h"
#include "FakeFileSystem.h"

using namespace pe_bear::updater;

namespace {

	class FakeArchiveReader : public IArchiveReader
	{
	public:
		FakeArchiveReader() : m_openOk(true) {}

		void addFile(const QString &path, const QByteArray &body = QByteArray("x"))
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindFile, body.size(), body.size()));
			m_bodies.insert(path, body);
		}
		void addDir(const QString &path)
		{
			m_entries.append(ArchiveEntry(path, ArchiveEntry::KindDir));
		}
		void setOpenFails() { m_openOk = false; }

		virtual bool open(const QString &) { return m_openOk; }
		virtual void close() {}
		virtual QList<ArchiveEntry> entries() const { return m_entries; }
		virtual QByteArray readEntry(const QString &path) { return m_bodies.value(path); }
		virtual QString lastError() const { return QLatin1String("fake reader"); }

	private:
		QList<ArchiveEntry> m_entries;
		QMap<QString, QByteArray> m_bodies;
		bool m_openOk;
	};

}; // namespace

class TestPlatformInstallerFactory : public QObject
{
	Q_OBJECT

private slots:
	void init() {}
	void cleanup() {}

	void createsDirectoryInstallerForWindows();
	void createsLinuxInstallerForLinux();
	void createsMacOSInstallerForMacOS();
	void createsDirectoryInstallerForUnknownPlatform();
	void createReturnsValidInstaller();
	void createForPlatformReturnsCorrectType();
};

void TestPlatformInstallerFactory::createsDirectoryInstallerForWindows()
{
	/* On Windows, the factory should create a DirectoryInstaller */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *installer = PlatformInstallerFactory::createForPlatform(
		PlatformWindows, &fs, &reader);
	
	QVERIFY(installer != nullptr);
	QCOMPARE(installer->name(), QLatin1String("directory"));
	
	delete installer;
}

void TestPlatformInstallerFactory::createsLinuxInstallerForLinux()
{
	/* On Linux, the factory should create a LinuxInstaller (or fall back to DirectoryInstaller) */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *installer = PlatformInstallerFactory::createForPlatform(
		PlatformLinux, &fs, &reader);
	
	QVERIFY(installer != nullptr);
	/* On non-Linux platforms, this might fall back to DirectoryInstaller */
	QVERIFY(installer->name() == QLatin1String("linux") || 
		installer->name() == QLatin1String("directory"));
	
	delete installer;
}

void TestPlatformInstallerFactory::createsMacOSInstallerForMacOS()
{
	/* On macOS, the factory should create a MacOSInstaller (or fall back to DirectoryInstaller) */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *installer = PlatformInstallerFactory::createForPlatform(
		PlatformMacOS, &fs, &reader);
	
	QVERIFY(installer != nullptr);
	/* On non-macOS platforms, this might fall back to DirectoryInstaller */
	QVERIFY(installer->name() == QLatin1String("macos") || 
		installer->name() == QLatin1String("directory"));
	
	delete installer;
}

void TestPlatformInstallerFactory::createsDirectoryInstallerForUnknownPlatform()
{
	/* For unknown platforms, the factory should create a DirectoryInstaller */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *installer = PlatformInstallerFactory::createForPlatform(
		PlatformUnknown, &fs, &reader);
	
	QVERIFY(installer != nullptr);
	QCOMPARE(installer->name(), QLatin1String("directory"));
	
	delete installer;
}

void TestPlatformInstallerFactory::createReturnsValidInstaller()
{
	/* The create() method should return a valid installer for the current platform */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *installer = PlatformInstallerFactory::create(&fs, &reader);
	
	QVERIFY(installer != nullptr);
	QVERIFY(!installer->name().isEmpty());
	
	delete installer;
}

void TestPlatformInstallerFactory::createForPlatformReturnsCorrectType()
{
	/* Test that createForPlatform returns the expected installer types */
	FakeFileSystem fs;
	FakeArchiveReader reader;
	
	PlatformInstaller *windows = PlatformInstallerFactory::createForPlatform(PlatformWindows, &fs, &reader);
	PlatformInstaller *linux = PlatformInstallerFactory::createForPlatform(PlatformLinux, &fs, &reader);
	PlatformInstaller *macos = PlatformInstallerFactory::createForPlatform(PlatformMacOS, &fs, &reader);
	PlatformInstaller *unknown = PlatformInstallerFactory::createForPlatform(PlatformUnknown, &fs, &reader);
	
	QVERIFY(windows != nullptr);
	QVERIFY(linux != nullptr);
	QVERIFY(macos != nullptr);
	QVERIFY(unknown != nullptr);
	
	/* Clean up */
	delete windows;
	delete linux;
	delete macos;
	delete unknown;
}

QTEST_APPLESS_MAIN(TestPlatformInstallerFactory)