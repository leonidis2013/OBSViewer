#include "core/Countdown.hpp"

namespace obsviewer {

CountdownTarget nextCountdown(int today, int nowSeconds, int targetMinutes, int daysMask, int graceSeconds)
{
	CountdownTarget t;
	if ((daysMask & kAllDays) == 0 || today < 1 || today > 7)
		return t;
	for (int offset = -1; offset <= 7; ++offset) {
		const int day = ((today - 1 + offset) % 7 + 7) % 7 + 1;
		if (!(daysMask & dayBit(day)))
			continue;
		const long diff = static_cast<long>(offset) * 86400L + targetMinutes * 60L - nowSeconds;
		if (diff >= -static_cast<long>(graceSeconds)) {
			t.found = true;
			t.dayOffset = offset;
			t.secondsTo = diff;
			return t;
		}
	}
	return t;
}

} // namespace obsviewer
