#include "DumpFileName.h"

QString DumpFileName::sanitize(const QString &name)
{
	QString safe = name;
	for (int i = 0; i < safe.length(); i++) {
		const ushort c = safe.at(i).unicode();
		const bool isAllowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == '.' || c == '_' || c == '-';
		if (!isAllowed) {
			safe[i] = QLatin1Char('_');
		}
	}
	return safe;
}

QString DumpFileName::forSection(const QString &peName, size_t secIndex, const QString &secName)
{
	return peName + "[" + QString::number(secIndex) + "_" + sanitize(secName) + "]";
}
