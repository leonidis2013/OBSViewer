#pragma once

namespace obsviewer {

// Bit for a weekday in Settings::countdownDays (1 = Monday … 7 = Sunday, like
// QDate::dayOfWeek).
constexpr int dayBit(int dayOfWeek)
{
	return 1 << (dayOfWeek - 1);
}
constexpr int kAllDays = 0x7F;

struct CountdownTarget {
	bool found = false;
	int dayOffset = 0;    // 0 = today, 1 = tomorrow … (-1 = yesterday, just after midnight)
	long secondsTo = 0;   // target − now; negative once it has started
};

// The countdown's target: the nearest start on a selected day that is still
// ahead, or started less than `graceSeconds` ago (so "+0:42 started" keeps
// showing for a while, even across midnight). today: 1 = Monday … 7 = Sunday.
CountdownTarget nextCountdown(int today, int nowSeconds, int targetMinutes, int daysMask, int graceSeconds = 3600);

} // namespace obsviewer
