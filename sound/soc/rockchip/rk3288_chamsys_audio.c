// SPDX-License-Identifier: GPL-2.0-only
/*
 * Machine ASoC driver for ChamSys MQ50HD and MQ70HD consoles, with
 * RK3288 Firefly Core board and ES8328 codec.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "rockchip_i2s.h"


#define CSYS_AUDIO_MCLK 11289600
#define CSYS_AUDIO_MCLK_FS 256
#define CSYS_AUDIO_LRCLK 44100

static unsigned char csys_es8328_regs[] = {
	0x00, 0x35, // ADC+DAC fs, power seq, ref
	0x03, 0x09, // ADC power
	0x09, 0x00, // ADC gain
	0x0a, 0x00, // ADC input select
	0x0b, 0x00, // ADC input select
	0x10, 0x00, // ADC left volume
	0x11, 0x00, // ADC right volume
	0x12, 0xea, // ADC ALC on, min/max gain
	0x13, 0xc0, // ADC ALC timing
	0x14, 0x05, // ADC ALC timing
	0x15, 0x06, // ADC ALC params
	0x16, 0x53, // ADC Gate on
	0x19, 0x02, // DAC mute, volume control
	0x1A, 0x0A, // DAC left volume
	0x1B, 0x0A, // DAC right volume
	0x26, 0x12, // DAC mixer
	0x27, 0xb8, // DAC mixer
	0x28, 0x38, // DAC mixer
	0x29, 0x38, // DAC mixer
	0x2A, 0xb8, // DAC mixer
	0x2e, 0x24, // DAC LOUT1 volume
	0x2f, 0x24, // DAC ROUT1 volume
	0x30, 0x00, // DAC LOUT2 volume
	0x31, 0x00, // DAC ROUT2 volume
	0xFF, 0xFF
};

unsigned char csys_es8328_regs_quiet[] = {
	0x2e, 0x00, // DAC LOUT1 volume
	0x2f, 0x00, // DAC ROUT1 volume
	0xFF, 0xFF
};

struct rk_drvdata {
	int spare;
};

static struct snd_soc_card csys_card;

static int csys_audio_set_mclk(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_dai *cpu = snd_soc_rtd_to_cpu(runtime, 0);
	struct snd_soc_dai *codec = snd_soc_rtd_to_codec(runtime, 0);

	ret = snd_soc_dai_set_sysclk(cpu, 0, CSYS_AUDIO_MCLK, SND_SOC_CLOCK_OUT);
	if(ret)
	{
		dev_err(cpu->dev, "Cannot set cpu MCLK\n");
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec, 0, CSYS_AUDIO_MCLK, SND_SOC_CLOCK_OUT);
	if(ret)
	{
		dev_err(codec->dev, "Cannot set codec MCLK\n");
		return ret;
	}

	return 0;
}

static int csys_audio_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	if(params_rate(params) != CSYS_AUDIO_LRCLK)
		return -EINVAL;

	ret = csys_audio_set_mclk(rtd);
	if(ret)
		return ret;

	return 0;
}

static void csys_audio_set_es8328_regs(struct snd_soc_component *codec, unsigned char regs[])
{
	int i;

	for(i = 0; regs[i] != 0xFF; i += 2)
		snd_soc_component_write(codec, regs[i], regs[i+1]);
}

static int csys_audio_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	csys_audio_set_es8328_regs(snd_soc_rtd_to_codec(rtd, 0)->component,
				   csys_es8328_regs);

	return 0;
}

static void csys_audio_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);

	csys_audio_set_es8328_regs(snd_soc_rtd_to_codec(rtd, 0)->component,
				   csys_es8328_regs_quiet);
}

static int csys_audio_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;

	ret = csys_audio_set_mclk(runtime);
	if(ret)
		return ret;

	// Keep audio out quiet during boot
	csys_audio_set_es8328_regs(snd_soc_rtd_to_codec(runtime, 0)->component,
				   csys_es8328_regs_quiet);

	return 0;
}

static const struct snd_soc_ops rk_ops = {
	.hw_params = csys_audio_hw_params,
	.startup = csys_audio_startup,
	.shutdown = csys_audio_shutdown,
};

SND_SOC_DAILINK_DEFS(links,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "HiFi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link csys_dailink = {
	.name = "Codecs",
	.stream_name = "Audio",
	.init = csys_audio_init,
	.ops = &rk_ops,
	/* Codec is the bit clock and frame consumer (SoC is master) */
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBC_CFC,
	SND_SOC_DAILINK_REG(links),
};

static const struct snd_soc_dapm_widget csys_widgets[] = {
	SND_SOC_DAPM_LINE("Line out", NULL),
	SND_SOC_DAPM_LINE("Line in", NULL),
};

static const struct snd_soc_dapm_route csys_routes[] = {
	{"Line out", NULL, "LOUT1"},
	{"Line out", NULL, "LOUT1"},
	{"LINPUT1", NULL, "Line in"},
	{"LINPUT1", NULL, "Line in"},
};

static const struct snd_kcontrol_new csys_controls[] = {
	SOC_DAPM_PIN_SWITCH("Line out"),
	SOC_DAPM_PIN_SWITCH("Line in"),
};

static struct snd_soc_card csys_card = {
	.owner = THIS_MODULE,
	.name = "chamsys-pcm",
	.dai_link = &csys_dailink,
	.num_links = 1,
	.num_aux_devs = 0,
	.dapm_widgets = csys_widgets,
	.num_dapm_widgets = ARRAY_SIZE(csys_widgets),
	.dapm_routes = csys_routes,
	.num_dapm_routes = ARRAY_SIZE(csys_routes),
	.controls = csys_controls,
	.num_controls = ARRAY_SIZE(csys_controls),
};

static int csys_audio_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &csys_card;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *tmpdn;
	struct rk_drvdata *machine;
	struct of_phandle_args args;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct rk_drvdata),
			       GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card->dev = &pdev->dev;
	//platform_set_drvdata(pdev, card);

	tmpdn = of_parse_phandle(np, "chamsys,audio-cpu", 0);
	if (!tmpdn) {
		dev_err(&pdev->dev,
			"Property 'chamsys,audio-cpu' missing or invalid\n");
		return -EINVAL;
	}
	csys_dailink.cpus[0].of_node = tmpdn;
	csys_dailink.platforms[0].of_node = tmpdn;

	tmpdn = of_parse_phandle(np, "chamsys,audio-codec", 0);
	if (!tmpdn) {
		dev_err(&pdev->dev,
			"Property 'chamsys,audio-codec' missing or invalid\n");
		return -EINVAL;
	}
	csys_dailink.codecs[0].of_node = tmpdn;

	// Set dai_name for codec as it is compulsory

	ret = of_parse_phandle_with_fixed_args(np, "chamsys,audio-codec",
					       0, 0, &args);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to parse property 'chamsys,audio-codec'\n");
		return ret;
	}

	ret = snd_soc_get_dai_name(&args, &csys_dailink.codecs[0].dai_name);
	if (ret) {
		dev_err(&pdev->dev, "Unable to get codec_dai_name\n");
		return ret;
	}

	snd_soc_card_set_drvdata(card, machine);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	if (ret) {
		dev_err(&pdev->dev,
			"Soc register card failed %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id chamsys_audio_driver_of_match[] = {
	{ .compatible = "rockchip,rk3288-chamsys-audio", },
	{},
};

MODULE_DEVICE_TABLE(of, chamsys_audio_driver_of_match);

static struct platform_driver chamsys_audio_driver = {
	.probe = csys_audio_probe,
	.driver = {
		.name = "rk3288-chamsys-audio",
		.pm = &snd_soc_pm_ops,
		.of_match_table = chamsys_audio_driver_of_match,
	},
};

module_platform_driver(chamsys_audio_driver);

MODULE_DESCRIPTION("ASoC machine driver for ChamSys MQ50HD/MQ70HD (RK3288 + ES8328)");
MODULE_LICENSE("GPL");
