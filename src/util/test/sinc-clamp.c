/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A full-scale edge rings past int16 range; the sinc output must saturate
// instead of wrapping to the opposite sign.

#include <mgba-util/interpolator.h>

#include <stdio.h>

static int16_t edge(int index, const void* context) {
	(void) context;
	return index < 0 ? INT16_MIN : INT16_MAX;
}

int main(void) {
	struct mInterpolatorSinc sinc;
	mInterpolatorSincInit(&sinc, 0, 0);
	struct mInterpolationData data = { .at = edge };

	int failures = 0;
	double t;
	for (t = -12; t <= 12; t += 0.01) {
		int16_t out = sinc.d.interpolate(&sinc.d, &data, t, 1.0);
		if ((t >= 1 && out < INT16_MAX / 2) || (t <= -2 && out > INT16_MIN / 2)) {
			printf("FAIL: t=%.2f out=%d\n", t, out);
			++failures;
			break;
		}
	}
	mInterpolatorSincDeinit(&sinc);

	printf("sinc clamp: %d failures\n", failures);
	return failures != 0;
}
