// SPDX-License-Identifier: GPL-2.0-only
/* Included by qcom_scm.c to test load validation without invoking SCM. */

#include <kunit/test.h>

struct qseecom_load_test {
	const char *name;
	u64 phys;
	size_t mdt_len;
	size_t img_len;
	enum qcom_scm_convention convention;
	int expected;
};

static const struct qseecom_load_test qseecom_load_tests[] = {
	{
		.name = "complete ELF without separate metadata",
		.phys = SZ_4M, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_32,
	}, {
		.name = "split image with metadata",
		.phys = SZ_4M, .mdt_len = SZ_1K, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_32,
	}, {
		.name = "metadata fills image",
		.phys = SZ_4M, .mdt_len = SZ_4K, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_64,
	}, {
		.name = "empty image",
		.phys = SZ_4M,
		.convention = SMC_CONVENTION_ARM_64,
		.expected = -EINVAL,
	}, {
		.name = "metadata exceeds image",
		.phys = SZ_4M, .mdt_len = SZ_4K + 1, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_64,
		.expected = -EINVAL,
	}, {
		.name = "failed TZ address translation",
		.img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_64,
		.expected = -EINVAL,
	}, {
		.name = "SMC32 image ends at last address",
		.phys = SZ_4G - SZ_4K, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_32,
	}, {
		.name = "SMC32 last address holds one byte",
		.phys = U32_MAX, .img_len = 1,
		.convention = SMC_CONVENTION_ARM_32,
	}, {
		.name = "SMC32 image crosses 4 GiB",
		.phys = SZ_4G - SZ_4K, .img_len = SZ_4K + 1,
		.convention = SMC_CONVENTION_ARM_32,
		.expected = -EOVERFLOW,
	}, {
		.name = "SMC32 image starts above 4 GiB",
		.phys = SZ_4G, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_32,
		.expected = -EOVERFLOW,
	}, {
		.name = "legacy SCM image crosses 4 GiB",
		.phys = U32_MAX, .img_len = 2,
		.convention = SMC_CONVENTION_LEGACY,
		.expected = -EOVERFLOW,
	}, {
		.name = "SMC64 image above 4 GiB",
		.phys = SZ_4G, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_64,
	}, {
		.name = "SMC64 image ends at last address",
		.phys = U64_MAX - SZ_4K + 1, .img_len = SZ_4K,
		.convention = SMC_CONVENTION_ARM_64,
	}, {
		.name = "physical range wraps",
		.phys = U64_MAX - SZ_4K + 1, .img_len = SZ_4K + 1,
		.convention = SMC_CONVENTION_ARM_64,
		.expected = -EOVERFLOW,
	},
};

static void qseecom_load_desc(const struct qseecom_load_test *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(qseecom_load, qseecom_load_tests, qseecom_load_desc);

static void qseecom_load_validate_test(struct kunit *test)
{
	const struct qseecom_load_test *param = test->param_value;
	int ret;

	ret = qcom_scm_qseecom_validate_load(param->phys, param->mdt_len,
					     param->img_len, param->convention);
	KUNIT_EXPECT_EQ(test, ret, param->expected);
}

static struct kunit_case qseecom_load_test_cases[] = {
	KUNIT_CASE_PARAM(qseecom_load_validate_test, qseecom_load_gen_params),
	{}
};

static struct kunit_suite qseecom_load_test_suite = {
	.name = "qcom_scm_qseecom_load",
	.test_cases = qseecom_load_test_cases,
};

kunit_test_suite(qseecom_load_test_suite);
