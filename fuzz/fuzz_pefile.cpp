/*
 * libFuzzer target: bearparser loading a PE from a buffer.
 *
 * Does what PE-bear does with a file the user opens -- ExeFactory recognises
 * it, builds a PEFile (whose constructor wraps the headers and every data
 * directory) -- and then reads every field of every wrapper and every entry
 * below it, the way the tree views do when they are expanded. Anything that
 * reads outside the input is a finding; a refusal by exception is not.
 */
#include <bearparser/bearparser.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

	const int MAX_DEPTH = 16;
	const size_t MAX_FIELDS = 256;
	const size_t MAX_ENTRIES = 4096;

	void walk(ExeElementWrapper *w, int depth)
	{
		if (!w || depth > MAX_DEPTH) return;

		(void) w->getName();
		(void) w->getContentSize();
		size_t fields = w->getFieldsCount();
		if (fields > MAX_FIELDS) fields = MAX_FIELDS;
		for (size_t f = 0; f < fields; f++) {
			(void) w->getFieldName(f);
			(void) w->translateFieldContent(f);
			(void) w->getWrappedValue(f).toQString();
		}

		ExeNodeWrapper *node = dynamic_cast<ExeNodeWrapper*>(w);
		if (!node) return;
		size_t entries = node->getEntriesCount();
		if (entries > MAX_ENTRIES) entries = MAX_ENTRIES;
		for (size_t i = 0; i < entries; i++) {
			walk(node->getEntryAt(i), depth + 1);
		}
	}

	void walkResources(PEFile *pe)
	{
		ResourcesAlbum *album = pe->getResourcesAlbum();
		if (!album) return;
		std::vector<pe::resource_type> types = album->getResourceTypes();
		for (size_t i = 0; i < types.size(); i++) {
			ResourcesContainer *c = album->getResourcesOfType(types[i]);
			if (!c) continue;
			const size_t count = c->count();
			for (size_t k = 0; k < count && k < MAX_ENTRIES; k++) {
				walk(c->getWrapperAt(k), 0);
			}
		}
	}

}; // namespace

extern "C" int LLVMFuzzerInitialize(int *, char ***)
{
	ExeFactory::init();
	// the builders live for the whole run; freed at exit so that the leak
	// check does not report them
	std::atexit(ExeFactory::destroy);
	return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size == 0) return 0;

	ByteBuffer *buf = nullptr;
	Executable *exe = nullptr;
	try {
		// A copy, padded like the buffers PE-bear itself reads files into.
		buf = new ByteBuffer(const_cast<BYTE*>(reinterpret_cast<const BYTE*>(data)), static_cast<bufsize_t>(size));
		if (ExeFactory::findMatching(buf) == ExeFactory::PE) {
			exe = ExeFactory::build(buf, ExeFactory::PE);
		}
		PEFile *pe = dynamic_cast<PEFile*>(exe);
		if (pe) {
			for (size_t i = 0; i < PEFile::WR_DIR_ENTRY_END; i++) {
				walk(pe->getWrapper(i), 0);
			}
			walkResources(pe);
		}
	} catch (CustomException &) {
		// refused: a correct outcome for malformed input
	}
	delete exe;
	delete buf;
	return 0;
}
