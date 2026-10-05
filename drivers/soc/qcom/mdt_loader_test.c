// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synthetic fixtures for the QSEECOM image helpers. The remoteproc loading
 * helpers have different semantics and are deliberately not used here.
 */

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/unaligned.h>

#define MDT_TEST_SIZE	512
#define MDT_TEST_PHOFF	129
#define MDT_TEST_PHNUM	4

struct mdt_test_image {
	struct firmware fw;
	bool elf64;
	u8 data[MDT_TEST_SIZE + 1];
};

static void mdt_test_segment(struct mdt_test_image *image, unsigned int index,
			     u32 type, u32 flags, u64 offset, u64 size)
{
	u8 *data = image->data + 1 + MDT_TEST_PHOFF;

	if (image->elf64) {
		struct elf64_phdr phdr = {
			.p_type = cpu_to_le32(type),
			.p_flags = cpu_to_le32(flags),
			.p_offset = cpu_to_le64(offset),
			.p_filesz = cpu_to_le64(size),
			.p_memsz = cpu_to_le64(size),
		};

		memcpy(data + index * sizeof(phdr), &phdr, sizeof(phdr));
	} else {
		struct elf32_phdr phdr = {
			.p_type = cpu_to_le32(type),
			.p_flags = cpu_to_le32(flags),
			.p_offset = cpu_to_le32(offset),
			.p_filesz = cpu_to_le32(size),
			.p_memsz = cpu_to_le32(size),
		};

		memcpy(data + index * sizeof(phdr), &phdr, sizeof(phdr));
	}
}

static void mdt_test_reset(struct mdt_test_image *image)
{
	u8 *data = image->data + 1;
	unsigned int i;

	image->fw.data = data;
	image->fw.size = MDT_TEST_SIZE;
	for (i = 0; i < MDT_TEST_SIZE; i++)
		data[i] = i ^ 0xa5;

	/* Both the ELF header and program-header table are unaligned. */
	if (image->elf64) {
		struct elf64_hdr ehdr = {
			.e_type = cpu_to_le16(ET_EXEC),
			.e_machine = cpu_to_le16(EM_AARCH64),
			.e_version = cpu_to_le32(EV_CURRENT),
			.e_ehsize = cpu_to_le16(sizeof(ehdr)),
			.e_phoff = cpu_to_le64(MDT_TEST_PHOFF),
			.e_phentsize = cpu_to_le16(sizeof(struct elf64_phdr)),
			.e_phnum = cpu_to_le16(MDT_TEST_PHNUM),
		};

		memcpy(data, &ehdr, sizeof(ehdr));
	} else {
		struct elf32_hdr ehdr = {
			.e_type = cpu_to_le16(ET_EXEC),
			.e_machine = cpu_to_le16(EM_ARM),
			.e_version = cpu_to_le32(EV_CURRENT),
			.e_ehsize = cpu_to_le16(sizeof(ehdr)),
			.e_phoff = cpu_to_le32(MDT_TEST_PHOFF),
			.e_phentsize = cpu_to_le16(sizeof(struct elf32_phdr)),
			.e_phnum = cpu_to_le16(MDT_TEST_PHNUM),
		};

		memcpy(data, &ehdr, sizeof(ehdr));
	}
	memcpy(data, ELFMAG, SELFMAG);
	data[EI_CLASS] = image->elf64 ? ELFCLASS64 : ELFCLASS32;
	data[EI_DATA] = ELFDATA2LSB;
	data[EI_VERSION] = EV_CURRENT;

	mdt_test_segment(image, 0, PT_NULL, 0, 400, 17);
	mdt_test_segment(image, 1, PT_LOAD, QCOM_MDT_TYPE_HASH, 420, 11);
	/* A zero-sized segment beyond EOF must not make the image split. */
	mdt_test_segment(image, 2, PT_LOAD, 0, U32_MAX, 0);
	mdt_test_segment(image, 3, PT_LOAD, 0, 460, 23);
}

static int mdt_test_init(struct kunit *test)
{
	struct mdt_test_image *image;
	const u8 *elf_class = test->param_value;

	image = kunit_kzalloc(test, sizeof(*image), GFP_KERNEL);
	if (!image)
		return -ENOMEM;
	image->elf64 = *elf_class == ELFCLASS64;
	mdt_test_reset(image);
	test->priv = image;
	return 0;
}

static void mdt_test_complete(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	size_t mdt_len = SIZE_MAX;
	u8 output[MDT_TEST_SIZE + 1];
	ssize_t ret;

	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
			(ssize_t)MDT_TEST_SIZE);
	KUNIT_EXPECT_EQ(test, mdt_len, (size_t)0);
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, NULL),
			(ssize_t)MDT_TEST_SIZE);

	memset(output, 0x5a, sizeof(output));
	ret = qcom_mdt_read_image(NULL, &image->fw, "qcom-mdt-test.mbn",
				  output, MDT_TEST_SIZE);
	KUNIT_ASSERT_EQ(test, ret, (ssize_t)MDT_TEST_SIZE);
	/* Include the hash, non-PT_LOAD data, gaps and trailing padding. */
	KUNIT_EXPECT_MEMEQ(test, output, image->fw.data, MDT_TEST_SIZE);
	KUNIT_EXPECT_EQ(test, output[MDT_TEST_SIZE], (u8)0x5a);
}

static void mdt_test_split_size(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	size_t mdt_len = SIZE_MAX;

	/* Only one segment need extend beyond EOF to select split assembly. */
	mdt_test_segment(image, 3, PT_LOAD, 0, MDT_TEST_SIZE - 22, 23);
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
			(ssize_t)(MDT_TEST_SIZE + 17 + 11 + 23));
	KUNIT_EXPECT_EQ(test, mdt_len, image->fw.size);
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, NULL),
			(ssize_t)(MDT_TEST_SIZE + 17 + 11 + 23));

	/* A segment ending exactly at EOF is still a complete image. */
	mdt_test_segment(image, 3, PT_LOAD, 0, MDT_TEST_SIZE - 23, 23);
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
			(ssize_t)MDT_TEST_SIZE);
	KUNIT_EXPECT_EQ(test, mdt_len, (size_t)0);
}

static void mdt_test_expect_error(struct kunit *test, int error)
{
	struct mdt_test_image *image = test->priv;
	size_t mdt_len = 0x1234;
	u8 output[MDT_TEST_SIZE], expected[MDT_TEST_SIZE];
	ssize_t ret;

	memset(output, 0x5a, sizeof(output));
	memset(expected, 0x5a, sizeof(expected));
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
			(ssize_t)error);
	KUNIT_EXPECT_EQ(test, mdt_len, (size_t)0x1234);
	KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, NULL),
			(ssize_t)error);
	ret = qcom_mdt_read_image(NULL, &image->fw, "qcom-mdt-test.mdt",
				  output, sizeof(output));
	KUNIT_EXPECT_EQ(test, ret, (ssize_t)error);
	KUNIT_EXPECT_MEMEQ(test, output, expected, sizeof(output));
}

static void mdt_test_bad_ident(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	static const unsigned int offsets[] = {
		EI_MAG0, EI_CLASS, EI_DATA, EI_VERSION,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		mdt_test_reset(image);
		image->data[1 + offsets[i]] = 0;
		kunit_info(test, "invalid e_ident[%u]\n", offsets[i]);
		mdt_test_expect_error(test, -EINVAL);
	}
	mdt_test_reset(image);
	image->data[1 + EI_DATA] = ELFDATA2MSB;
	mdt_test_expect_error(test, -EINVAL);
}

static void mdt_test_truncated_headers(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	size_t ehsize = image->elf64 ? sizeof(struct elf64_hdr) :
				     sizeof(struct elf32_hdr);
	size_t phsize = image->elf64 ? sizeof(struct elf64_phdr) :
				     sizeof(struct elf32_phdr);
	size_t sizes[] = {
		0, EI_NIDENT - 1, ehsize - 1,
		MDT_TEST_PHOFF + MDT_TEST_PHNUM * phsize - 1,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		image->fw.size = sizes[i];
		kunit_info(test, "truncated size %zu\n", sizes[i]);
		mdt_test_expect_error(test, -EINVAL);
	}
}

static void mdt_test_bad_phdr_table(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	u8 *data = image->data + 1;
	size_t phnum = image->elf64 ? offsetof(struct elf64_hdr, e_phnum) :
				    offsetof(struct elf32_hdr, e_phnum);
	size_t phentsize = image->elf64 ? offsetof(struct elf64_hdr, e_phentsize) :
					offsetof(struct elf32_hdr, e_phentsize);

	put_unaligned_le16(0, data + phnum);
	mdt_test_expect_error(test, -EINVAL);
	mdt_test_reset(image);
	put_unaligned_le16(1, data + phentsize);
	mdt_test_expect_error(test, -EINVAL);
	mdt_test_reset(image);
	put_unaligned_le16(U16_MAX, data + phnum);
	mdt_test_expect_error(test, -EINVAL);
	mdt_test_reset(image);
	if (image->elf64)
		put_unaligned_le64(MDT_TEST_SIZE, data + offsetof(struct elf64_hdr, e_phoff));
	else
		put_unaligned_le32(MDT_TEST_SIZE, data + offsetof(struct elf32_hdr, e_phoff));
	mdt_test_expect_error(test, -EINVAL);

	/* ELF64 addition wraps; ELF32 must not wrap at 32 bits either. */
	if (image->elf64)
		put_unaligned_le64(U64_MAX - 1, data + offsetof(struct elf64_hdr, e_phoff));
	else
		put_unaligned_le32(U32_MAX - 1, data + offsetof(struct elf32_hdr, e_phoff));
	mdt_test_expect_error(test, -EINVAL);
}

static void mdt_test_overflow(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	size_t mdt_len = SIZE_MAX;

	if (image->elf64) {
		mdt_test_segment(image, 3, PT_LOAD, 0, U64_MAX, 1);
		mdt_test_expect_error(test, -EINVAL);
		/* No segment range wraps, but their combined size does. */
		mdt_test_segment(image, 3, PT_LOAD, 0, 0, U64_MAX);
		mdt_test_expect_error(test, -EOVERFLOW);
		mdt_test_segment(image, 3, PT_LOAD, 0, 0, SSIZE_MAX);
		mdt_test_expect_error(test, -EOVERFLOW);
	} else {
		mdt_test_segment(image, 3, PT_LOAD, 0, U32_MAX, 23);
		KUNIT_EXPECT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
				(ssize_t)(MDT_TEST_SIZE + 17 + 11 + 23));
		KUNIT_EXPECT_EQ(test, mdt_len, image->fw.size);
	}

	mdt_test_reset(image);
	image->fw.size = (size_t)SSIZE_MAX + 1;
	mdt_test_expect_error(test, -EOVERFLOW);
}

static void mdt_test_undersized_output(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	u8 output[MDT_TEST_SIZE], expected[MDT_TEST_SIZE];
	ssize_t ret;

	memset(output, 0x5a, sizeof(output));
	memset(expected, 0x5a, sizeof(expected));
	ret = qcom_mdt_read_image(NULL, &image->fw, "qcom-mdt-test.mbn",
				  output, MDT_TEST_SIZE - 1);
	KUNIT_EXPECT_EQ(test, ret, (ssize_t)-ENOSPC);
	KUNIT_EXPECT_MEMEQ(test, output, expected, sizeof(output));

	mdt_test_segment(image, 3, PT_LOAD, 0, MDT_TEST_SIZE, 23);
	ret = qcom_mdt_read_image(NULL, &image->fw, "qcom-mdt-test.mdt",
				  output, sizeof(output));
	KUNIT_EXPECT_EQ(test, ret, (ssize_t)-ENOSPC);
	KUNIT_EXPECT_MEMEQ(test, output, expected, sizeof(output));
}

static void mdt_test_split_read(struct kunit *test)
{
	struct mdt_test_image *image = test->priv;
	static const char * const names[] = {
		"qcom-mdt-test.b00", "qcom-mdt-test.b01", "qcom-mdt-test.b03",
	};
	static const unsigned int indices[] = { 0, 1, 3 };
	struct firmware segments[ARRAY_SIZE(names)] = {};
	struct device *dev;
	size_t size = image->fw.size, written, mdt_len;
	u8 *output;
	unsigned int i;
	ssize_t ret;

	/*
	 * Use the real firmware loader, not a replacement for the MDT parser
	 * or the segment-loading path. The minimal config embeds these blobs.
	 */
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		if (!firmware_request_builtin(&segments[i], names[i]))
			kunit_skip(test, "requires mdt_loader_test.config firmware fixtures");
		mdt_test_segment(image, indices[i], i ? PT_LOAD : PT_NULL,
				 i == 1 ? QCOM_MDT_TYPE_HASH : 0,
				 MDT_TEST_SIZE + i * 100, segments[i].size);
		size += segments[i].size;
	}
	dev = kunit_device_register(test, "qcom-mdt-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	output = kunit_kmalloc(test, size + 1, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, output);
	memset(output, 0x5a, size + 1);
	KUNIT_ASSERT_EQ(test, qcom_mdt_get_image_size(&image->fw, &mdt_len),
			(ssize_t)size);
	KUNIT_EXPECT_EQ(test, mdt_len, image->fw.size);
	ret = qcom_mdt_read_image(dev, &image->fw, "qcom-mdt-test.mdt",
				  output, size);
	KUNIT_ASSERT_EQ(test, ret, (ssize_t)size);
	KUNIT_EXPECT_MEMEQ(test, output, image->fw.data, image->fw.size);
	written = image->fw.size;
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		KUNIT_EXPECT_MEMEQ(test, output + written, segments[i].data,
				   segments[i].size);
		written += segments[i].size;
	}
	KUNIT_EXPECT_EQ(test, output[size], (u8)0x5a);

	/* The last split file is shorter than its advertised p_filesz. */
	mdt_test_segment(image, 3, PT_LOAD, 0, MDT_TEST_SIZE, segments[2].size + 1);
	ret = qcom_mdt_read_image(dev, &image->fw, "qcom-mdt-test.mdt",
				  output, size + 1);
	KUNIT_EXPECT_EQ(test, ret, (ssize_t)-EINVAL);
}

static const u8 mdt_test_classes[] = { ELFCLASS32, ELFCLASS64 };

static void mdt_test_class_name(const u8 *elf_class, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "ELF%u",
		 *elf_class == ELFCLASS64 ? 64 : 32);
}

KUNIT_ARRAY_PARAM(mdt_test_class, mdt_test_classes, mdt_test_class_name);

static struct kunit_case mdt_loader_test_cases[] = {
	KUNIT_CASE_PARAM(mdt_test_complete, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_split_size, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_bad_ident, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_truncated_headers, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_bad_phdr_table, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_overflow, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_undersized_output, mdt_test_class_gen_params),
	KUNIT_CASE_PARAM(mdt_test_split_read, mdt_test_class_gen_params),
	{}
};

static struct kunit_suite mdt_loader_test_suite = {
	.name = "qcom_mdt_loader",
	.init = mdt_test_init,
	.test_cases = mdt_loader_test_cases,
};

kunit_test_suite(mdt_loader_test_suite);

MODULE_DESCRIPTION("Qualcomm MDT image loader tests");
MODULE_IMPORT_NS("TEST_FIRMWARE");
MODULE_LICENSE("GPL");
