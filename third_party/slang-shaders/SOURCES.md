# CRT shader data

Copied unmodified from https://github.com/libretro/slang-shaders at commit `2345ee91602a7404d3a1a6a5df6108d7305fdad4`
by `tools/make_shaders.py`. They are data files loaded at run time by librashader.
Most are GPL-licensed; each file keeps its own notice, and the licence files found
beside them are included.

## Presets

| Menu | Preset |
|---|---|
| CRT-ROYALE | `crt/crt-royale.slangp` |
| ROYALE PVM | `presets/crt-royale-kurozumi.slangp` |
| GUEST ADVANCED | `crt/crt-guest-advanced.slangp` |
| GUEST NTSC | `crt/crt-guest-advanced-ntsc.slangp` |
| CRT-GEOM | `crt/crt-geom.slangp` |
| CRT-LOTTES | `crt/crt-lottes.slangp` |
| CRT-HYLLIAN | `crt/crt-hyllian.slangp` |
| EASYMODE | `crt/crt-easymode.slangp` |
| APERTURE | `crt/crt-aperture.slangp` |
| CONSUMER TV | `crt/crt-consumer.slangp` |
| CALIGARI | `crt/crt-caligari.slangp` |
| ZFAST | `crt/zfast-crt.slangp` |
| GUEST HD | `crt/crt-guest-advanced-hd.slangp` |
| GDV MINI | `crt/crt-gdv-mini.slangp` |
| GEOM DELUXE | `crt/crt-geom-deluxe.slangp` |

## Files (118, 1703324 bytes)

| File | Licence words in its header | SHA-256 |
|---|---|---|
| `blurs/shaders/royale/blur5fast-horizontal.slang` | MIT | `886b2c077afba6e31822259948ef5bbd5a7a7beaccf6210f838f3bfb11e9fe52` |
| `blurs/shaders/royale/blur5fast-vertical.slang` | MIT | `9c275d50bd358ecbbdc23e7ddd2708e1a1c834dbde1310b84bc3a124348fbf6e` |
| `blurs/shaders/royale/blur9fast-horizontal.slang` | MIT | `402bf7ff14c5c3825a52dde087ec550f067ab53752f884f77dfdbbcaa495172b` |
| `blurs/shaders/royale/blur9fast-vertical.slang` | MIT | `4da274ed27c9a3a1055b9b76162c5b3f18f71acfdf9cc2e8f7ffc8282298fd51` |
| `blurs/shaders/royale/vertex-shader-blur-fast-horizontal.h` | MIT | `be12271987eabf9d9ae02b0d2365f50012368b224476da7d50bb71cbcf07e1e2` |
| `blurs/shaders/royale/vertex-shader-blur-fast-vertical.h` | MIT | `b076f4a4b3b8fadb5bc80d0b4b9b045de839a6bd75a755b866af7e06a483ca94` |
| `crt/crt-aperture.slangp` |  | `71ea5ae6ca681ac45e74bb1af694d72a5bebf80a272b0766a2e7f1f62d78cc06` |
| `crt/crt-caligari.slangp` |  | `f25678e34331f2ef3b09888bdcefeb2d4db9a9520b5df46f4c3d1b74538d5b18` |
| `crt/crt-consumer.slangp` |  | `b8d3e286fda027ffb5e346d3b537b5f333668dd9d01dc88f7821b38d4c346907` |
| `crt/crt-easymode.slangp` |  | `3e624916ca4e3bc8660c0b00b3abcb262b03b2699137a03d8171d936054a1fd5` |
| `crt/crt-gdv-mini.slangp` |  | `571a47584e9006a4c5c95dce4abbe58a527378a822f381f81651a3c41c800053` |
| `crt/crt-geom-deluxe.slangp` |  | `3a6c2200a4d35ea4f0150d9927cdf9e1ecef8ad19ada5bdbb85bd5e37c160bb3` |
| `crt/crt-geom.slangp` |  | `094d5133a3b6fbb4c2e6cba65141e5b850517aa3201bea7bdb41e4899087b60b` |
| `crt/crt-guest-advanced-hd.slangp` |  | `1cf2a61bce42a6fd6679f3bbf50b0a94194dcf7e498b6b3abdf9572aefcc7e40` |
| `crt/crt-guest-advanced-ntsc.slangp` |  | `d4e66ed1250ae9101971f5174978ae82061c1e3901397832984d523c543a8163` |
| `crt/crt-guest-advanced.slangp` |  | `eb597473e15639e9dec4f11a18d4a789f5f901e9368816d4e5114b09b51c2d19` |
| `crt/crt-hyllian.slangp` |  | `f858cdb6fb9fa2786f85584a66915429e73c9c0cf7b8aea8548b213d2a17363f` |
| `crt/crt-lottes.slangp` |  | `51bdf93aa6e3dd8605bf1c2b19af1f511672b53f3551d8de1041e02a7bbf9610` |
| `crt/crt-royale.slangp` |  | `724e0196cf83da1a229bc896e970d639b4cbeb9c41f341e1e5764780fecede96` |
| `crt/shaders/crt-aperture.slang` | GPL | `2cbadf75f1dfc4741504c2fbcd323c5c77d663500de92ea1c43ad21fd8202a69` |
| `crt/shaders/crt-caligari.slang` | GPL | `771aa239edc54483792c78dd07c0cc7f9a31ee7d31af1c7b70ba02be04c190e3` |
| `crt/shaders/crt-consumer.slang` | GPL | `ca69f7c89c5f38cd8f325e2f35a06f8ddcd01c210469cde3fbc935d2144c958e` |
| `crt/shaders/crt-easymode.slang` |  | `cacb1595c7988619c29b46cc283e2140f25cf14a9d104fcf6c28f197ee4b29a3` |
| `crt/shaders/crt-gdv-mini.slang` | GPL | `3627dcebb5b0b69cf3c529045edd0a21fea9631d508d65ffe168f93203f479df` |
| `crt/shaders/crt-geom.slang` |  | `6533ad97601445f35a1b4f25ad54974ee9983fef0337ffec11c9c372226dbb58` |
| `crt/shaders/crt-lottes.slang` | public domain | `e382eec413c2acf5d03cce65d447347553032acf2e550826d0142a9c51d559ef` |
| `crt/shaders/crt-royale/LICENSE.TXT` |  | `d8c320ffc0030d1b096ae4732b50d2b811cf95e9a9b7377c1127b2563e0a0388` |
| `crt/shaders/crt-royale/src/bind-shader-params.h` | GPL | `0c96863109297914837fa80c5639d10ca780c584ee3817edaca19ca7d452de0e` |
| `crt/shaders/crt-royale/src/bloom-functions.h` | GPL | `8bc5450359a056ff8dbd9b2504f467687aefb8018af7cc44cc2f222c61c3d0c3` |
| `crt/shaders/crt-royale/src/crt-royale-bloom-approx.h` | GPL | `50d5af04fa770560fac17a3f00bbfcab417ad87cc7fa702cdea0b2da78261c80` |
| `crt/shaders/crt-royale/src/crt-royale-bloom-approx.slang` |  | `9e531b3c3046fe41c6576f5c2ab836442bcc258d0d4ed3f0669631592fc154e4` |
| `crt/shaders/crt-royale/src/crt-royale-bloom-horizontal-reconstitute.slang` | GPL | `89a64e73f93bb3d6d6156114784b3b9e2a5b8289ca58fdaca06ee177bdebe5bb` |
| `crt/shaders/crt-royale/src/crt-royale-bloom-vertical.slang` | GPL | `0605645255aabeb03d3a2e9c7fe6e028dff5e12ea0568f2b8ed04ac4e95fa4cd` |
| `crt/shaders/crt-royale/src/crt-royale-brightpass.slang` | GPL | `6748bf8ca0779b652992cb13d5c0f0ce7927a80e55f64d9fd97ba311b708ed9c` |
| `crt/shaders/crt-royale/src/crt-royale-first-pass-linearize-crt-gamma-bob-fields.slang` | GPL | `08d609d05ff1e6ca793c0969a1f1c393f2960eb3c734c43235428eb5fa94f2a0` |
| `crt/shaders/crt-royale/src/crt-royale-geometry-aa-last-pass.h` | GPL | `a4e73af234fb5bd8470b50efdd3704f2cf0f6f57ee3b9e6400edbe3247216dce` |
| `crt/shaders/crt-royale/src/crt-royale-geometry-aa-last-pass.slang` |  | `062aa057e96ce109281b1936bf1deb2b8ad6b3daa13eae7d5f889f08ea83b27d` |
| `crt/shaders/crt-royale/src/crt-royale-mask-resize-horizontal.slang` | GPL | `966b8d1de40773a9f68e33dc9e7420e62511c7dbb30fed1f80b3426a9e74896e` |
| `crt/shaders/crt-royale/src/crt-royale-mask-resize-vertical.slang` | GPL | `c20a113e76cb32765c42d5f0c018a39955ca0142f893c6ee8bec2721f2c25c27` |
| `crt/shaders/crt-royale/src/crt-royale-scanlines-horizontal-apply-mask.h` | GPL | `87439a49f06ba4bcbdfd2fa38b55c0e444ebe3958f6f8653edf5b1dc80ada0d2` |
| `crt/shaders/crt-royale/src/crt-royale-scanlines-horizontal-apply-mask.slang` |  | `09fc3f28678ba4b270d3cf736f427bac14245136f81e6cdbcdf7ab933d0eb4aa` |
| `crt/shaders/crt-royale/src/crt-royale-scanlines-vertical-interlacing.slang` | GPL | `a93600fa01fe13c9a76868dad47e6901389721efa66db8aa3cf71448184f1fe2` |
| `crt/shaders/crt-royale/src/derived-settings-and-constants.h` | GPL | `1438399a93acd0adf54831f37fb20199d81b3828802658b7a8ea5ed0aa58ca91` |
| `crt/shaders/crt-royale/src/geometry-functions.h` | GPL | `0a4e93cc2f6bf524d7b83b777817fde65c83d7c5a7e390eda640e966bb5ad53d` |
| `crt/shaders/crt-royale/src/phosphor-mask-resizing.h` | GPL | `1820dede5dc179511f3a6d8ad1bb709a85aac2b21ac635eaaaef72ec75532ac0` |
| `crt/shaders/crt-royale/src/scanline-functions.h` | GPL | `e9b104537329ddc6c593418e19898ab15c91da06e76c018d1f54d6b4e4757b93` |
| `crt/shaders/crt-royale/src/tex2Dantialias.h` | GPL | `c2d69eb9989c59e3635849838414ad781a66f5f483a5dba781ad2649fa8014db` |
| `crt/shaders/crt-royale/src/user-cgp-constants.h` |  | `8976660d604283a5ee07fa39561559c9766e60323c655571a919a0d8d5518b6e` |
| `crt/shaders/crt-royale/TileableLinearApertureGrille15Wide8And5d5Spacing.png` |  | `4b46103e20567b99e9525ce521438573f49609452d142438088fa6309902aef2` |
| `crt/shaders/crt-royale/TileableLinearApertureGrille15Wide8And5d5SpacingResizeTo64.png` |  | `3ba708dba5989741c58d9b7a1d1e38499bb6dabeb6787da523c9ca6e06159215` |
| `crt/shaders/crt-royale/TileableLinearShadowMaskEDP.png` |  | `637e567fe5180ba53af5a185ceef24b4a44b86d74d802fb33df6a2d02d3bcf49` |
| `crt/shaders/crt-royale/TileableLinearShadowMaskEDPResizeTo64.png` |  | `d68808ef5c819555c6001ea2d0627c18e64a7d0a7a6ebb3af4a09261a4c23713` |
| `crt/shaders/crt-royale/TileableLinearSlotMaskTall15Wide9And4d5Horizontal9d14VerticalSpacing.png` |  | `4d4385d8476faaaebc6c4f6540b73c28bf3b643a6efb93866e58050f3d0f6567` |
| `crt/shaders/crt-royale/TileableLinearSlotMaskTall15Wide9And4d5Horizontal9d14VerticalSpacingResizeTo64.png` |  | `b3ab2c1d9205e167c44fc86aeb5a031a4fe7199de728d52f94e11635a467ab6d` |
| `crt/shaders/crt-royale/user-settings.h` |  | `fdd85fca181afaf64b112773721a57e040995b92d1364dcc1577b04538d11a04` |
| `crt/shaders/geom-deluxe/crt-geom-deluxe.slang` | GPL | `8decc17f567a87e01a70dde087b2725c475c9dc5576503dedc08a235f41959aa` |
| `crt/shaders/geom-deluxe/gaussx.slang` |  | `715ecab26c9dcd3f3c91a1b62391288effb062e5b52ead706fc81c5532afd2a4` |
| `crt/shaders/geom-deluxe/gaussy.slang` |  | `e14237098aa4cc2009dac4bad851f3b6c1399f955c598946355d7d5d46ed2950` |
| `crt/shaders/geom-deluxe/geom-deluxe-params.inc` |  | `db24b38923845ad5374a2d89aba57382184e311a314c8242509d1cb71df3fc59` |
| `crt/shaders/geom-deluxe/masks/aperture_2_4_rgb.png` |  | `b1bc1c4393d66299d4f44f5b5fc416396563cf02828ccf02b298b9d114c8e58b` |
| `crt/shaders/geom-deluxe/masks/delta_2_4x1_rgb.png` |  | `cb1698d91f0eec82a6f75565a8e65a364251cf045ddc6d95f4fe8ca33b46b192` |
| `crt/shaders/geom-deluxe/masks/slot_2_5x4_bgr.png` |  | `e5f5ff39af0347fc8165e5d08f68aae6d5e6c340e5ea5ce644e9d5e8c78ee08a` |
| `crt/shaders/geom-deluxe/phosphor_apply.slang` |  | `c2d8cbc03f25ce55ad91d35ffc953b95b5c72878a31899a599f2bc80b6704a89` |
| `crt/shaders/geom-deluxe/phosphor_update.slang` |  | `8ca471fe5f29bae518d8a63ba65bac4f4639dedd6c378a08619a45b98d2b24b2` |
| `crt/shaders/guest/advanced/afterglow0.slang` | GPL | `7a53266418477402d00ae20848142fc5db795befc25e5870edcb03b28d0dbafd` |
| `crt/shaders/guest/advanced/avg-lum-ntsc.slang` | GPL | `30d27513e322a7c82654f7c70b2a7a34eaeff554761e40cba7756b8f4068cbb4` |
| `crt/shaders/guest/advanced/avg-lum.slang` | GPL | `a486df346043dc4d5d21b38b6f364b7a5f0b430e4a8220869a6adeb6ae9e7328` |
| `crt/shaders/guest/advanced/bloom_horizontal.slang` | GPL | `a5ab3547d19990144bf3e0e80abcc7dc8a541cb1ca80ec44a9464c3307cb7972` |
| `crt/shaders/guest/advanced/bloom_vertical.slang` | GPL | `e7992b156937ce826572ae72e7e379d4e548b1de8315d82c4de625ba5f9e8b6b` |
| `crt/shaders/guest/advanced/crt-guest-advanced-ntsc-pass1.slang` | GPL | `d499fad2c65b03c36a3057e1efc8e74d9d9aa67edf72ca23ac18c992453cee64` |
| `crt/shaders/guest/advanced/crt-guest-advanced-ntsc-pass2.slang` | GPL | `b094a856579913a5207dba2a4d449ba764d523bb58fd3c8c6e5be5a6fc35ccf8` |
| `crt/shaders/guest/advanced/crt-guest-advanced.slang` | GPL | `713bf7cee57c41ebf54d6d4f9bd153715e5ef315cc313b9521e1220fc4f48320` |
| `crt/shaders/guest/advanced/custom-fast-sharpen.slang` | GPL | `ef91e3d4f564a8fe4f097aaaec46c0611867ba2e122b180c9355c8a9a41810b8` |
| `crt/shaders/guest/advanced/deconvergence-ntsc.slang` | GPL | `93326a53f688c31f01d740f50bd5de0efabb96e952edb27631c7d86e826c9ef0` |
| `crt/shaders/guest/advanced/deconvergence.slang` | GPL | `0a94521bfe23ee6c2bd67986260d7671619258e0975689e98be565cbad0e11c5` |
| `crt/shaders/guest/advanced/gaussian_horizontal.slang` | GPL | `31c3999ac5b0f6dd784039a22ab3da3c56cd2141eeaedf7b3821c831556e9e53` |
| `crt/shaders/guest/advanced/gaussian_vertical.slang` | GPL | `869e6d0eb1b630ab8e5b3ff9868df3cf9a7a6ba33d768dcc9ecc0ea93b36b359` |
| `crt/shaders/guest/advanced/interlace-ntsc.slang` | GPL | `313ad5f188ff39749f450a0a59ff850a4fc8d311b4cb3ea8197c81facf26ffa1` |
| `crt/shaders/guest/advanced/linearize-ntsc-new.slang` | GPL | `5100d2a252697a2fdc938f87aa9d92faf08586513fb381fd759f18568204cf00` |
| `crt/shaders/guest/advanced/linearize.slang` | GPL | `a17af1c157c730583e11a9306d737a60eb80a880ed1edc51c3b5aa29155d7e39` |
| `crt/shaders/guest/advanced/lut/inv-trinitron-lut.png` |  | `2acb6633e4dede7f36e3e62b7aa9d0ed76ecfbc5cb7ed7f3f8813dddec0e9145` |
| `crt/shaders/guest/advanced/lut/nec-lut.png` |  | `86ec3d2e21138845cb73500e915425582b991e173a4149fa192a62d798382b59` |
| `crt/shaders/guest/advanced/lut/ntsc-lut.png` |  | `a23ae9d27d6d5f9073d4a84678187f54758b329387c47686294ea979dcde6d03` |
| `crt/shaders/guest/advanced/lut/trinitron-lut.png` |  | `bcc8c237eb39ed2a632554959cb4c5e0dd52b59a4922745b46b7525fc6b6b61a` |
| `crt/shaders/guest/advanced/ntsc/ntsc-pass1.slang` |  | `31788bb4b9929e66ed656ff4b32bb419dec744263f4f4ac7c989ca300481fbc3` |
| `crt/shaders/guest/advanced/ntsc/ntsc-pass2.slang` |  | `572cb5da08482742ba849b04f65f2ce4bbde8bba495213e7c2e9069582ba2aaa` |
| `crt/shaders/guest/advanced/ntsc/ntsc-pass3.slang` |  | `1b8f9c6bd9738355498155e49be6491ff7b6ab7e436994974bbdeea4163d9a6b` |
| `crt/shaders/guest/advanced/pre-shaders-afterglow.slang` | GPL | `1c4e57ff95acc881d761d95196fce8e3000701750868187907c4cffad5c6d4b7` |
| `crt/shaders/guest/advanced/stock.slang` |  | `fa7d346e3706a351aa77dc8e77bd049b8bfde635baf4376fedcf81f38d9a4008` |
| `crt/shaders/guest/hd/afterglow0.slang` | GPL | `9fe82bb9763b9c5991d92ab6f1b423ca578a1d258a8b8e9ba64cd23e80c55c96` |
| `crt/shaders/guest/hd/bloom_horizontal.slang` | GPL | `417824f3056a57067ad57b6bb973aa321450fd3033f5c39b375d1e478e2bb074` |
| `crt/shaders/guest/hd/bloom_vertical.slang` | GPL | `7020fa020a431aaba71010a06652287dcb21050db364e5848043ef0f49f59a6f` |
| `crt/shaders/guest/hd/crt-guest-advanced-hd-pass1.slang` | GPL | `da202cae6930a4d1a8519ec31ca77277c5d25d7b5a67c96281d7400738436dfa` |
| `crt/shaders/guest/hd/crt-guest-advanced-hd-pass2.slang` | GPL | `f48a4ab1a8b800107630d0828e516e998bf5cd546e004d70e1604ba24c7a496a` |
| `crt/shaders/guest/hd/deconvergence-hd.slang` | GPL | `3b0b652740d7b66d1f33146d3bd6225924f02a326b98495a2d743b0dd8eefc70` |
| `crt/shaders/guest/hd/gaussian_horizontal.slang` | GPL | `a74c7251b0cd71092611b2b696fd737e9f635525dbce7d8b5da10a664af1f225` |
| `crt/shaders/guest/hd/gaussian_vertical.slang` | GPL | `869e6d0eb1b630ab8e5b3ff9868df3cf9a7a6ba33d768dcc9ecc0ea93b36b359` |
| `crt/shaders/guest/hd/linearize-hd.slang` | GPL | `35c24c494dc6ba38f64386976c7e56421d161dd1e398a73ece2ecdba708ce659` |
| `crt/shaders/guest/hd/pre-shaders-afterglow.slang` | GPL | `1906f72720142e9eb4757e4c440c7146ece993517778d85a08c624f93293a8f7` |
| `crt/shaders/guest/hd/stock.slang` |  | `fa7d346e3706a351aa77dc8e77bd049b8bfde635baf4376fedcf81f38d9a4008` |
| `crt/shaders/hyllian/crt-hyllian-base.slang` |  | `c763fba0a737ef68cc88b710c82c747e1f02bb4b99c02db64944c5504682e513` |
| `crt/shaders/hyllian/support/glow/blur-glow-mask-geom.slang` |  | `1a06c42a4fe3f0c62f68e0b11dca132e5a3640aae574f87dfbb69a241f36099f` |
| `crt/shaders/hyllian/support/glow/blur_horiz.slang` |  | `3618511b73e2124af028621b4885904df3b0f88c65e691b49de5d33009e1fcbd` |
| `crt/shaders/hyllian/support/glow/threshold.slang` |  | `653e853465ac18326070f1a2d860f230c72d4aea18feccb6af8a0f224fd1957c` |
| `crt/shaders/hyllian/support/LUT/Sony_Wega_29FA310_no_gamma-v2.png` |  | `f8a3131e2782d64f548b3cbbfdfdc77cc9b72169940f50d73916811c6f9a5e6f` |
| `crt/shaders/hyllian/support/LUT/Sony_Wega_29FA310_no_gamma.png` |  | `303392f028ce76aa91ccc4ed82fcd705b4a074f7ab5bdcbdac00d25a5e986369` |
| `crt/shaders/hyllian/support/multiLUT-linear-fast.slang` |  | `0a63d9df5ff1e07a9507053268ffab0760816217fcfe39591c3825279d738735` |
| `crt/shaders/zfast_crt/zfast_crt_finemask.slang` |  | `0214419beed4c08d991288e90f8a73700fd7da567a454ad2ffe725b3b6eea366` |
| `crt/shaders/zfast_crt/zfast_crt_impl.inc` | GPL | `e1006274577b85f18ed793856b46f92d5cd6f6207a98af398004644006907bf1` |
| `crt/zfast-crt.slangp` |  | `fa3a7f37344f351572e692bc317a21f89f30628c08c09adc1091e4900f1f8e11` |
| `include/blur-functions.h` | MIT | `0edea684af38d783068f61eccc7c8ff8695c80133605bab3f8e73a2337e307d6` |
| `include/compat_macros.inc` |  | `38c1957b4bedc098ce01f2ee56db6aa97e708b037087dfde51d4ebbe342a2de6` |
| `include/gamma-management.h` | MIT | `aa851dac11e468ef0b168bb8576f709b386ea35f5a9f10ed16c0789343a0ad0e` |
| `include/quad-pixel-communication.h` | MIT | `e07f8c0b8583bf78babe1d23333b83ebae40c3f5a664e35fdd041ae6fccb5410` |
| `include/special-functions.h` | MIT | `0b5dbbaef443327756958b0f59caad87e23f13f499e0fcd5bac97860b069b3ab` |
| `include/subpixel_masks.h` | public domain | `540385326e9f4bb0aedd4e669f59dd7ce947b1232ea3fc6b9ca6b685aac6c22c` |
| `misc/shaders/grade.slang` | GPL | `10642b3c8c857ad8d42e8414d59c79572bcbe27bf7c5be62ee77272cca357b23` |
| `presets/crt-royale-kurozumi.slangp` |  | `891f77b5ab82e8e38deb18613c6b9163cb6ff3f350f75579a7bc199e760a6569` |
