/*
 * Covers CDisasm's ownership of its capstone handle and instruction buffer.
 *
 * The disassembly view calls init() again on every scroll, and an export calls
 * it once per chunk, so whatever one init() opens must be released by the next
 * one and by the destructor. Capstone lets the embedder supply its allocator;
 * the test installs a counting one and checks nothing is left outstanding.
 */
#include <QtTest>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <capstone/capstone.h>
#include "cdis/CDisasm.h"

namespace {

	std::set<void*> g_live;

	void* countingMalloc(size_t size)
	{
		void *p = std::malloc(size);
		if (p) g_live.insert(p);
		return p;
	}

	void* countingCalloc(size_t n, size_t size)
	{
		void *p = std::calloc(n, size);
		if (p) g_live.insert(p);
		return p;
	}

	void* countingRealloc(void *ptr, size_t size)
	{
		void *p = std::realloc(ptr, size);
		if (p) {
			if (ptr) g_live.erase(ptr);
			g_live.insert(p);
		}
		return p;
	}

	void countingFree(void *ptr)
	{
		if (ptr) g_live.erase(ptr);
		std::free(ptr);
	}

	/** CDisasm leaves the address translation to its subclass; flat addresses are enough here. */
	class FlatDisasm : public pe_bear::CDisasm
	{
	public:
		offset_t convertToRVA(offset_t raw) const { return raw; }
		offset_t convertToVA(offset_t raw) const { return raw; }
		offset_t getTargetVA(int, bool &isOk) const { isOk = false; return INVALID_ADDR; }
	};

	// a few x86 instructions: push ebp; mov ebp, esp; xor eax, eax; pop ebp; ret
	uint8_t g_code[] = { 0x55, 0x89, 0xE5, 0x31, 0xC0, 0x5D, 0xC3 };

} // namespace

class TestCDisasm : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		cs_opt_mem mem;
		mem.malloc = countingMalloc;
		mem.calloc = countingCalloc;
		mem.realloc = countingRealloc;
		mem.free = countingFree;
		mem.vsnprintf = vsnprintf;
		QCOMPARE(cs_option(0, CS_OPT_MEM, (size_t)&mem), CS_ERR_OK);
	}

	void init_disassembles()
	{
		FlatDisasm dis;
		QVERIFY(dis.init(g_code, sizeof(g_code), sizeof(g_code), 0, Executable::ARCH_INTEL, Executable::BITS_32));
		QVERIFY(dis.fillTable());
		QCOMPARE(dis.chunksCount(), size_t(5));
		QCOMPARE(dis.mnemStr(4), QString("ret "));
	}

	void destructor_releasesCapstone()
	{
		g_live.clear();
		{
			FlatDisasm dis;
			QVERIFY(dis.init(g_code, sizeof(g_code), sizeof(g_code), 0, Executable::ARCH_INTEL, Executable::BITS_32));
			QVERIFY(dis.fillTable());
			QVERIFY(!g_live.empty());
		}
		QCOMPARE(g_live.size(), size_t(0));
	}

	void reinit_releasesThePreviousHandle()
	{
		FlatDisasm dis;
		QVERIFY(dis.init(g_code, sizeof(g_code), sizeof(g_code), 0, Executable::ARCH_INTEL, Executable::BITS_32));
		const size_t afterFirst = g_live.size();
		for (int i = 0; i < 50; i++) {
			QVERIFY(dis.init(g_code, sizeof(g_code), sizeof(g_code), 0, Executable::ARCH_INTEL, Executable::BITS_32));
			QVERIFY(dis.fillTable());
		}
		QCOMPARE(g_live.size(), afterFirst);
	}
};

QTEST_MAIN(TestCDisasm)
#include "tst_cdisasm.moc"
