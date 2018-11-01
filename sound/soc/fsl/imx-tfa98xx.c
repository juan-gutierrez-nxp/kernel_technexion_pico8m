/*
 * ASoC driver for BBB + NXP TFA98xx family of devices
 *
 * Author:      Sebastien Jan <sjan@baylibre.com>
 * Copyright (C) 2015 NXP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define DEBUG
#define pr_fmt(fmt) "%s(): " fmt, __func__

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <sound/jack.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

#include <asm/dma.h>
#include <asm-generic/types.h>
#include "fsl_sai.h"

static u32 imx_tfa98xx_rates[] = { 8000, 16000, 32000, 44100, 48000 };
static struct snd_pcm_hw_constraint_list imx_tfa98xx_rate_constraints = {
	.count = ARRAY_SIZE(imx_tfa98xx_rates),
	.list = imx_tfa98xx_rates,
};

struct snd_soc_card_drvdata_imx_tfa {
	struct snd_soc_dai_link *dai;
	struct snd_soc_card card;
	int num_codecs;
};

static int imx_tfa98xx_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	int ret;

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &imx_tfa98xx_rate_constraints);
	if (ret)
		return ret;

	return 0;
}

static int imx_tfa98xx_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card_drvdata_imx_tfa *data = snd_soc_card_get_drvdata(rtd->card);
	u32 channels = params_channels(params);
	struct snd_soc_dai *codec_dai;
	unsigned int fmt;
	int i, ret;

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(cpu_dai->dev, "failed to set cpu dai fmt\n");
		return ret;
	}

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];
		ret = snd_soc_dai_set_fmt(codec_dai, fmt);
		if (ret) {
			dev_err(codec_dai->dev,
				"failed to set codec dai[%d] fmt\n", i);
			return ret;
		}
	}

	return ret;
}

static struct snd_soc_ops imx_tfa98xx_ops = {
	.startup = imx_tfa98xx_startup,
	.hw_params = imx_tfa98xx_hw_params,
};

static void *tfa_devm_kstrdup(struct device *dev, char *buf)
{
	char *str = devm_kzalloc(dev, strlen(buf) + 1, GFP_KERNEL);

	pr_info("\n");
	if (!str)
		return str;
	memcpy(str, buf, strlen(buf));
	return str;
}

#if defined(CONFIG_OF)
/*
 * The structs are used as place holders. They will be completely
 * filled with data from dt node.
 */

static struct snd_soc_dai_link imx_dai_tfa98xx[] = {
	{
		.name = "tfa98xx",
		.stream_name = "Audio",
		.ops = &imx_tfa98xx_ops,
	},
	{
		/* sentinel */
	},
};

static const struct of_device_id imx_tfa98_dt_ids[] = {
	{
		.compatible = "nxp,imx-audio-tfa98xx",
		.data = &imx_dai_tfa98xx,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_tfa98_dt_ids);

static const struct snd_soc_dapm_widget imx_tfa98xx_widgets[] = {
	SND_SOC_DAPM_LINE("Speaker", NULL),
	SND_SOC_DAPM_LINE("DMIC", NULL),
};

static struct snd_soc_card imx_tfa98xx_soc_card = {
	.owner = THIS_MODULE,
	.name = "TFA9912",	/* default name if none defined in DT */
	.dai_link = imx_dai_tfa98xx,
	.num_links = ARRAY_SIZE(imx_dai_tfa98xx),
	.dapm_widgets = imx_tfa98xx_widgets,
	.num_dapm_widgets = ARRAY_SIZE(imx_tfa98xx_widgets),
};

static int imx_tfa98xx_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np, *np = pdev->dev.of_node;
	struct device_node *codec_np_0 = NULL, *codec_np_1 = NULL;
	struct snd_soc_dai_link_component *codecs = NULL;
	struct platform_device *cpu_pdev;
	struct snd_soc_dai_link *dai;
	struct i2c_client *codec_dev;
	struct snd_soc_card_drvdata_imx_tfa *drvdata = NULL;
	int ret = 0;

	pr_info("\n");

	imx_tfa98xx_soc_card.dev = &pdev->dev;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		ret = -ENOMEM;
		goto fail;
	}

	cpu_np = of_parse_phandle(pdev->dev.of_node, "audio-cpu", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "cpu dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "failed to find SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_np_0 = of_parse_phandle(np, "audio-codec", 0);
	if (!codec_np_0) {
		dev_err(&pdev->dev, "audio codec phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_dev = of_find_i2c_device_by_node(codec_np_0);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "fail to find codec 0 platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_np_1 = of_parse_phandle(np, "audio-codec", 1);
	if (!codec_np_1) {
		dev_err(&pdev->dev, "audio codec phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_dev = of_find_i2c_device_by_node(codec_np_1);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "fail to find codec 1 platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	drvdata->num_codecs = 2;
	codecs = devm_kzalloc(&pdev->dev,
		drvdata->num_codecs * sizeof(struct snd_soc_dai_link_component),
		GFP_KERNEL);
	if (!codecs) {
		ret = -ENOMEM;
		goto fail;
	}

	codecs[0].of_node = codec_np_0;
	codecs[0].dai_name = "tfa98xx-aif-1-34";
	codecs[1].of_node = codec_np_1;
	codecs[1].dai_name = "tfa98xx-aif-1-35";

	dai = &imx_dai_tfa98xx[0];
	dai->platform_of_node = cpu_np;
	dai->codecs = codecs;
	dai->cpu_dai_name = dev_name(&cpu_pdev->dev);
	dai->num_codecs = 2;
	dai->cpu_of_node = cpu_np;

	ret = snd_soc_of_parse_card_name(&imx_tfa98xx_soc_card, "nxp,model");
	if (ret)
		goto fail;

	ret = snd_soc_of_parse_audio_routing(&imx_tfa98xx_soc_card, "nxp,audio-routing");
	if (ret)
		goto fail;

	imx_tfa98xx_soc_card.num_links = 1;
	imx_tfa98xx_soc_card.dai_link = dai;
	imx_tfa98xx_soc_card.owner = THIS_MODULE;

	platform_set_drvdata(pdev, &drvdata->card);
	snd_soc_card_set_drvdata(&imx_tfa98xx_soc_card, drvdata);

	ret = devm_snd_soc_register_card(&pdev->dev, &imx_tfa98xx_soc_card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);

	pr_info("done\n");
	return ret;
fail:
	if (cpu_np)
		of_node_put(cpu_np);
	if (codec_np_0)
		of_node_put(codec_np_0);
	if (codec_np_1)
		of_node_put(codec_np_1);

	return ret;
}

static int imx_tfa98xx_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct snd_soc_card_drvdata_imx_tfa *drvdata =
				snd_soc_card_get_drvdata(card);


	return 0;
}

static struct platform_driver imx_tfa98xx_driver = {
	.probe		= imx_tfa98xx_probe,
	.remove		= imx_tfa98xx_remove,
	.driver		= {
		.name	= "imx-tfa98xx",
		.owner	= THIS_MODULE,
		.pm	= &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(imx_tfa98_dt_ids),
	},
};
#endif

static int __init _tfa98xx_init(void)
{
#if defined(CONFIG_OF)
	if (of_have_populated_dt())
		return platform_driver_register(&imx_tfa98xx_driver);
#endif
	return 0;
}

static void __exit _tfa98xx_exit(void)
{
#if defined(CONFIG_OF)
	if (of_have_populated_dt()) {
		platform_driver_unregister(&imx_tfa98xx_driver);
		return;
	}
#endif
}

module_init(_tfa98xx_init);
module_exit(_tfa98xx_exit);

MODULE_AUTHOR("Jerry Yoon");
MODULE_DESCRIPTION("i.MX7d TFA98XX ASoC driver");
MODULE_LICENSE("GPL");
