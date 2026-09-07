// SPDX-License-Identifier: ISC
/* Included by htt_rx.c to exercise the actual in-order RX implementation. */
#include <kunit/device.h>
#include <kunit/test.h>

struct ath10k_rx_test_param {
	const char *name;
	const struct ath10k_htt_rx_desc_ops *ops;
	bool target_64bit;
};

static const struct ath10k_rx_test_param ath10k_rx_test_params[] = {
	{ "paddr32_qca988x", &qca988x_rx_desc_ops, false },
	{ "paddr64_wcn3990", &wcn3990_rx_desc_ops, true },
};

static void ath10k_rx_test_param_desc(const struct ath10k_rx_test_param *param,
				      char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ath10k_rx, ath10k_rx_test_params, ath10k_rx_test_param_desc);

struct ath10k_rx_test_ctx {
	struct ath10k *ar;
	struct sk_buff_head amsdu;
	struct sk_buff *event;
};

static int ath10k_rx_test_init(struct kunit *test)
{
	const struct ath10k_rx_test_param *param = test->param_value;
	struct ath10k_rx_test_ctx *ctx;
	struct ath10k *ar;
	struct device *dev;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ar = kunit_kzalloc(test, sizeof(*ar), GFP_KERNEL);
	if (!ar)
		return -ENOMEM;
	dev = kunit_device_register(test, "ath10k-rx-test");
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ar->dev = dev;
	ar->hw_params.rx_desc_ops = param->ops;
	ar->hw_params.target_64bit = param->target_64bit;
	ar->htt.ar = ar;
	ar->htt.rx_ring.in_ord_rx = true;
	spin_lock_init(&ar->htt.rx_ring.lock);
	hash_init(ar->htt.rx_ring.skb_table);
	skb_queue_head_init(&ar->htt.rx_in_ord_split);
	skb_queue_head_init(&ar->htt.rx_msdus_q);
	skb_queue_head_init(&ctx->amsdu);
	ctx->ar = ar;
	test->priv = ctx;
	return 0;
}

static void ath10k_rx_test_exit(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct ath10k_skb_rxcb *rxcb;
	struct hlist_node *tmp;
	struct sk_buff *skb;
	int bucket;

	kfree_skb(ctx->event);
	skb_queue_purge(&ctx->amsdu);
	skb_queue_purge(&ctx->ar->htt.rx_in_ord_split);
	skb_queue_purge(&ctx->ar->htt.rx_msdus_q);
	hash_for_each_safe(ctx->ar->htt.rx_ring.skb_table, bucket, tmp, rxcb, hlist) {
		skb = ATH10K_RXCB_SKB(rxcb);
		dma_unmap_single(ctx->ar->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb), DMA_FROM_DEVICE);
		hash_del(&rxcb->hlist);
		kfree_skb(skb);
	}
}

/* Populate a descriptor before mapping it, as firmware would before unmap. */
static struct sk_buff *ath10k_rx_test_post(struct kunit *test, bool last,
					   bool done, unsigned int size)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct ath10k_hw_params *hw = &ctx->ar->hw_params;
	struct ath10k_skb_rxcb *rxcb;
	struct htt_rx_desc *rxd;
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = dev_alloc_skb(size + HTT_RX_DESC_ALIGN);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	memset(skb->data, 0, skb_tailroom(skb));
	rxd = HTT_RX_BUF_TO_RX_DESC(hw, skb->data);
	ath10k_htt_rx_desc_get_attention(hw, rxd)->flags =
		done ? __cpu_to_le32(RX_ATTENTION_FLAGS_MSDU_DONE) : 0;
	ath10k_htt_rx_desc_get_msdu_end(hw, rxd)->info0 =
		last ? __cpu_to_le32(RX_MSDU_END_INFO0_LAST_MSDU) : 0;
	paddr = dma_map_single(ctx->ar->dev, skb->data, skb_tailroom(skb),
			       DMA_FROM_DEVICE);
	if (dma_mapping_error(ctx->ar->dev, paddr)) {
		kfree_skb(skb);
		KUNIT_FAIL(test, "test DMA mapping failed");
		return NULL;
	}
	rxcb = ATH10K_SKB_RXCB(skb);
	rxcb->paddr = paddr;
	/* Every pop must reset this, including offload and monitor buffers. */
	rxcb->rx_len_invalid = true;
	hash_add(ctx->ar->htt.rx_ring.skb_table, &rxcb->hlist, paddr);
	ctx->ar->htt.rx_ring.fill_cnt++;
	return skb;
}

static struct htt_rx_in_ord_ind *ath10k_rx_test_event(struct kunit *test,
						      unsigned int count)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_resp *resp;
	unsigned int size;

	kfree_skb(ctx->event);
	size = sizeof(resp->hdr) + sizeof(resp->rx_in_ord_ind) + count *
		(ctx->ar->hw_params.target_64bit ?
		 sizeof(struct htt_rx_in_ord_msdu_desc_ext) :
		 sizeof(struct htt_rx_in_ord_msdu_desc));
	ctx->event = alloc_skb(size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->event);
	resp = (void *)skb_put_zero(ctx->event, size);
	resp->rx_in_ord_ind.msdu_count = __cpu_to_le16(count);
	return &resp->rx_in_ord_ind;
}

static void ath10k_rx_test_entry(struct kunit *test,
				 struct htt_rx_in_ord_ind *ev, unsigned int i,
				 struct sk_buff *skb, u16 len)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	u64 paddr = ATH10K_SKB_RXCB(skb)->paddr;

	if (ctx->ar->hw_params.target_64bit) {
		ev->msdu_descs64[i].msdu_paddr = __cpu_to_le64(paddr);
		ev->msdu_descs64[i].msdu_len = __cpu_to_le16(len);
		ev->msdu_descs64[i].reserved = 1;
	} else {
		KUNIT_ASSERT_LE(test, paddr, (u64)U32_MAX);
		ev->msdu_descs32[i].msdu_paddr = __cpu_to_le32(paddr);
		ev->msdu_descs32[i].msdu_len = __cpu_to_le16(len);
		ev->msdu_descs32[i].reserved = 1;
	}
}

static int ath10k_rx_test_pop(struct kunit *test, struct htt_rx_in_ord_ind *ev)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct ath10k_htt *htt = &ctx->ar->htt;
	int ret;

	spin_lock_bh(&htt->rx_ring.lock);
	if (ctx->ar->hw_params.target_64bit)
		ret = ath10k_htt_rx_pop_paddr64_list(htt, ev, &htt->rx_in_ord_split);
	else
		ret = ath10k_htt_rx_pop_paddr32_list(htt, ev, &htt->rx_in_ord_split);
	spin_unlock_bh(&htt->rx_ring.lock);
	return ret;
}

static int ath10k_rx_test_indicate(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	int ret;

	spin_lock_bh(&ctx->ar->htt.rx_ring.lock);
	ret = ath10k_htt_rx_in_ord_ind(ctx->ar, ctx->event);
	spin_unlock_bh(&ctx->ar->htt.rx_ring.lock);
	return ret;
}

static int ath10k_rx_test_extract(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;

	return ath10k_htt_rx_extract_amsdu(&ctx->ar->hw_params,
					 &ctx->ar->htt.rx_in_ord_split, &ctx->amsdu);
}

static void ath10k_rx_length_boundary_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	int capacity = ath10k_htt_rx_msdu_size(&ctx->ar->hw_params);
	const u16 lengths[] = { 0, capacity - 1, capacity, capacity + 1,
				1994, 2799, U16_MAX };
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;
	bool invalid;
	int i;

	for (i = 0; i < ARRAY_SIZE(lengths); i++) {
		skb = ath10k_rx_test_post(test, true, true, HTT_RX_BUF_SIZE);
		KUNIT_ASSERT_NOT_NULL(test, skb);
		ev = ath10k_rx_test_event(test, 1);
		ath10k_rx_test_entry(test, ev, 0, skb, lengths[i]);
		invalid = lengths[i] > capacity;
		KUNIT_ASSERT_EQ(test, ath10k_rx_test_pop(test, ev), 0);
		KUNIT_EXPECT_EQ(test, skb->len, invalid ? 0U : lengths[i]);
		KUNIT_EXPECT_EQ(test, ATH10K_SKB_RXCB(skb)->rx_len_invalid, invalid);
		KUNIT_EXPECT_EQ(test, ath10k_rx_test_extract(test), invalid ? -EMSGSIZE : 0);
		KUNIT_EXPECT_EQ(test, ctx->ar->htt.rx_ring.fill_cnt, 0);
		KUNIT_EXPECT_TRUE(test, hash_empty(ctx->ar->htt.rx_ring.skb_table));
		skb_queue_purge(&ctx->amsdu);
	}
}

static void ath10k_rx_physical_tailroom_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	unsigned int desc_size = ctx->ar->hw_params.rx_desc_ops->rx_desc_size;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;
	int capacity;

	skb = ath10k_rx_test_post(test, true, true, desc_size + 64);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	capacity = skb_tailroom(skb) - desc_size;
	KUNIT_ASSERT_LT(test, capacity, ath10k_htt_rx_msdu_size(&ctx->ar->hw_params));
	ev = ath10k_rx_test_event(test, 1);
	ath10k_rx_test_entry(test, ev, 0, skb, capacity + 1);
	KUNIT_ASSERT_EQ(test, ath10k_rx_test_pop(test, ev), 0);
	KUNIT_EXPECT_EQ(test, skb->len, 0U);
	KUNIT_EXPECT_EQ(test, ath10k_rx_test_extract(test), -EMSGSIZE);
}

static void ath10k_rx_invalid_aggregate_member_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;
	unsigned int bad, i;

	for (bad = 1; bad <= 3; bad++) {
		ev = ath10k_rx_test_event(test, 5);
		for (i = 0; i < 5; i++) {
			skb = ath10k_rx_test_post(test, i == 0 || i >= 3, true,
						  HTT_RX_BUF_SIZE);
			KUNIT_ASSERT_NOT_NULL(test, skb);
			ath10k_rx_test_entry(test, ev, i, skb, i == bad ? U16_MAX : 100);
		}
		KUNIT_ASSERT_EQ(test, ath10k_rx_test_pop(test, ev), 0);
		KUNIT_EXPECT_EQ(test, ctx->ar->htt.rx_ring.fill_cnt, 0);
		KUNIT_ASSERT_EQ(test, ath10k_rx_test_extract(test), 0);
		KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->amsdu), 1U);
		skb_queue_purge(&ctx->amsdu);
		KUNIT_ASSERT_EQ(test, ath10k_rx_test_extract(test), -EMSGSIZE);
		KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->amsdu), 3U);
		skb_queue_purge(&ctx->amsdu);
		KUNIT_ASSERT_EQ(test, ath10k_rx_test_extract(test), 0);
		KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->amsdu), 1U);
		skb_queue_purge(&ctx->amsdu);
		KUNIT_EXPECT_TRUE(test, skb_queue_empty(&ctx->ar->htt.rx_in_ord_split));
	}
}

static void ath10k_rx_split_aggregate_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;
	unsigned int bad, i;

	/* Put the invalid member on either side of the indication boundary. */
	for (bad = 0; bad < 2; bad++) {
		for (i = 0; i < 2; i++) {
			ev = ath10k_rx_test_event(test, 1);
			skb = ath10k_rx_test_post(test, i == 1, true, HTT_RX_BUF_SIZE);
			KUNIT_ASSERT_NOT_NULL(test, skb);
			ath10k_rx_test_entry(test, ev, 0, skb, i == bad ? U16_MAX : 100);
			KUNIT_EXPECT_EQ(test, ath10k_rx_test_indicate(test), i == 0 ? -EIO : 0);
			KUNIT_EXPECT_FALSE(test, ctx->ar->htt.rx_confused);
			KUNIT_EXPECT_EQ(test, ctx->ar->htt.rx_ring.fill_cnt, 0);
			KUNIT_EXPECT_EQ(test, skb_queue_len(&ctx->ar->htt.rx_in_ord_split),
					i == 0 ? 1U : 0U);
		}
	}
}

static void ath10k_rx_continue_after_drop_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;
	unsigned int round, i;

	/* The real caller must drain healthy neighbours and the next indication.
	 * No channel is registered in this fixture, so normal payloads are later
	 * discarded by the existing channel filter, after successful extraction.
	 */
	for (round = 0; round < 2; round++) {
		ev = ath10k_rx_test_event(test, 3);
		for (i = 0; i < 3; i++) {
			skb = ath10k_rx_test_post(test, true, true, HTT_RX_BUF_SIZE);
			KUNIT_ASSERT_NOT_NULL(test, skb);
			ath10k_rx_test_entry(test, ev, i, skb,
					     round == 0 && i == 1 ? U16_MAX : 100);
		}
		KUNIT_EXPECT_EQ(test, ath10k_rx_test_indicate(test), 0);
		KUNIT_EXPECT_FALSE(test, ctx->ar->htt.rx_confused);
		KUNIT_EXPECT_EQ(test, ctx->ar->htt.rx_ring.fill_cnt, 0);
		KUNIT_EXPECT_TRUE(test, hash_empty(ctx->ar->htt.rx_ring.skb_table));
		KUNIT_EXPECT_TRUE(test, skb_queue_empty(&ctx->ar->htt.rx_in_ord_split));
		KUNIT_EXPECT_TRUE(test, skb_queue_empty(&ctx->ar->htt.rx_msdus_q));
	}
}

static void ath10k_rx_incomplete_dma_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;

	ev = ath10k_rx_test_event(test, 1);
	skb = ath10k_rx_test_post(test, true, false, HTT_RX_BUF_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	ath10k_rx_test_entry(test, ev, 0, skb, U16_MAX);
	KUNIT_EXPECT_EQ(test, ath10k_rx_test_indicate(test), -EIO);
	KUNIT_EXPECT_TRUE(test, ctx->ar->htt.rx_confused);
	KUNIT_EXPECT_EQ(test, ctx->ar->htt.rx_ring.fill_cnt, 0);
	KUNIT_EXPECT_TRUE(test, skb_queue_empty(&ctx->ar->htt.rx_in_ord_split));
}

static void ath10k_rx_offload_bypass_test(struct kunit *test)
{
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;

	ev = ath10k_rx_test_event(test, 1);
	ev->info = HTT_RX_IN_ORD_IND_INFO_OFFLOAD_MASK;
	skb = ath10k_rx_test_post(test, true, false, HTT_RX_BUF_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	ath10k_rx_test_entry(test, ev, 0, skb, U16_MAX);
	KUNIT_ASSERT_EQ(test, ath10k_rx_test_pop(test, ev), 0);
	KUNIT_EXPECT_EQ(test, skb->len, 0U);
	KUNIT_EXPECT_FALSE(test, ATH10K_SKB_RXCB(skb)->rx_len_invalid);
}

static void ath10k_rx_monitor_bypass_test(struct kunit *test)
{
	struct ath10k_rx_test_ctx *ctx = test->priv;
	struct htt_rx_in_ord_ind *ev;
	struct sk_buff *skb;

	ctx->ar->monitor_arvif = kunit_kzalloc(test, sizeof(struct ath10k_vif), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx->ar->monitor_arvif);
	ev = ath10k_rx_test_event(test, 1);
	skb = ath10k_rx_test_post(test, true, true, HTT_RX_BUF_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, skb);
	ath10k_rx_test_entry(test, ev, 0, skb, 100);
	KUNIT_ASSERT_EQ(test, ath10k_rx_test_pop(test, ev), 0);
	KUNIT_EXPECT_EQ(test, skb->len, 100U);
	KUNIT_EXPECT_FALSE(test, ATH10K_SKB_RXCB(skb)->rx_len_invalid);
	KUNIT_EXPECT_EQ(test, ath10k_rx_test_extract(test), 0);
}

static struct kunit_case ath10k_rx_test_cases[] = {
	KUNIT_CASE_PARAM(ath10k_rx_length_boundary_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_physical_tailroom_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_invalid_aggregate_member_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_split_aggregate_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_continue_after_drop_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_incomplete_dma_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_offload_bypass_test, ath10k_rx_gen_params),
	KUNIT_CASE_PARAM(ath10k_rx_monitor_bypass_test, ath10k_rx_gen_params),
	{}
};

static struct kunit_suite ath10k_rx_test_suite = {
	.name = "ath10k-in-order-rx",
	.init = ath10k_rx_test_init,
	.exit = ath10k_rx_test_exit,
	.test_cases = ath10k_rx_test_cases,
};

kunit_test_suite(ath10k_rx_test_suite);
