/*
 * Copyright 2018 NXP.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <sound/soc.h>

#include "fsl_sai.h"

#define IMX_SPH064X_FMTBIT \
	(SNDRV_PCM_FMTBIT_S32_LE | \
	 SNDRV_PCM_FMTBIT_S24_LE | \
	 SNDRV_PCM_FMTBIT_S16_LE)

struct imx_sph064x_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
};

static u32 imx_sph064x_rates[] = { 32000, 48000, 64000 };
static struct snd_pcm_hw_constraint_list imx_sph064x_rate_constrains = {
	.count = ARRAY_SIZE(imx_sph064x_rates),
	.list = imx_sph064x_rates,
};
static u32 imx_sph064x_channels[] = { 1, 2 };
static struct snd_pcm_hw_constraint_list imx_sph064x_channels_constrains = {
	.count = ARRAY_SIZE(imx_sph064x_channels),
	.list = imx_sph064x_channels,
};

static int imx_sph064x_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_card *card = rtd->card;
	int ret;

	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			&imx_sph064x_rate_constrains);
	if (ret) {
		dev_err(card->dev,
			"fail to set pcm hw rate constrains: %d\n", ret);
		return ret;
	}

	ret = snd_pcm_hw_constraint_list(runtime, 0,
		SNDRV_PCM_HW_PARAM_CHANNELS, &imx_sph064x_channels_constrains);
	if (ret) {
		dev_err(card->dev,
			"fail to set pcm hw channels constrains: %d\n", ret);
		return ret;
	}

	ret = snd_pcm_hw_constraint_mask64(runtime, SNDRV_PCM_HW_PARAM_FORMAT,
			IMX_SPH064X_FMTBIT);
	if (ret) {
		dev_err(card->dev,
			"fail to set pcm hw format constrains: %d\n", ret);
		return ret;
	}

	return 0;
}

static int imx_sph064x_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_card *card = rtd->card;
	unsigned int fmt;
	int ret;

	fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | \
		SND_SOC_DAIFMT_CBS_CFS;
	/* set cpu dai format configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, fmt);
	if (ret) {
		dev_err(card->dev, "fail to set cpu dai fmt: %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, 0, 0, 2, 32);
	if (ret) {
		dev_err(card->dev, "fail to set cpu dai tdm slots: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct snd_soc_ops imx_sph064x_ops = {
	.startup = imx_sph064x_startup,
	.hw_params = imx_sph064x_hw_params,
};

static int imx_sph064x_probe(struct platform_device *pdev)
{
	struct device_node *cpu_np = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct platform_device *cpu_pdev;
	struct imx_sph064x_data *data;
	int ret;

	cpu_np = of_parse_phandle(np, "audio-cpu", 0);
	if (!cpu_np) {
		dev_err(&pdev->dev, "cpu dai phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(&pdev->dev, "fail to find SAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	data->dai.name = "sph064x hifi";
	data->dai.stream_name = "sph064x hifi";
	data->dai.codec_dai_name = "snd-soc-dummy-dai";
	data->dai.codec_name = "snd-soc-dummy";
	data->dai.cpu_dai_name = dev_name(&cpu_pdev->dev);
	data->dai.platform_of_node = cpu_np;
	data->dai.ops = &imx_sph064x_ops;
	data->dai.capture_only = "true";

	data->card.dev = &pdev->dev;
	data->card.owner = THIS_MODULE;

	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret) {
		dev_err(&pdev->dev, "fail to find card model name\n");
		goto fail;
	}

	data->card.num_links = 1;
	data->card.dai_link = &data->dai;

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd soc register card failed: %d\n", ret);
		goto fail;
	}

	ret = 0;
fail:
	if (cpu_np)
		of_node_put(cpu_np);

	return ret;
}

static int imx_sph064x_remove(struct platform_device *pdev)
{
	struct imx_sph064x_data *data = platform_get_drvdata(pdev);
	/* unregister card */
	snd_soc_unregister_card(&data->card);
	return 0;
}

static const struct of_device_id imx_sph064x_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-sph064x", },
	{ /* sentinel*/ }
};
MODULE_DEVICE_TABLE(of, imx_sph064x_dt_ids);

static struct platform_driver imx_sph064x_driver = {
	.driver = {
		.name = "imx-sph064x",
		.pm = &snd_soc_pm_ops,
		.of_match_table = imx_sph064x_dt_ids,
	},
	.probe = imx_sph064x_probe,
	.remove = imx_sph064x_remove,
};
module_platform_driver(imx_sph064x_driver);

MODULE_DESCRIPTION("NXP i.MX I2S MEM mic ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-sph064x");
