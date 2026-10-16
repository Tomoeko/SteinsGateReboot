# Texture mask shader recovery

The readable HLSL in `shaders/reference/texture_mask_*.hlsl` and the masked copied blend programs recover original runtime pixel programs2042–2071 and1148/1150. Thirty-two original containers are represented by22 sources because phase3/5 programs share identical bytecode.

All32 complete containers match byte for byte after actual native compilation through the inspected D3DCompiler47 version10.0.18362.1 (SHA-256 `4b074a3976399dc735484f5d43d04b519b7bdee8ac719d9ab8ed6bd4e6be0345`). Inputs: entry `main`, profile `ps_4_0`, flags0, no macros or include handler, source name `recovered_shader.hlsl`. This demonstrates compatibility, not exact historical compiler provenance.

Target `Files/STEINS.GATE.REBOOT/sgre_steam.exe`, SHA-256 `75a83a0e2a7e22055417ae0474b47be98418c4e42c695c548b558705c404b9d8`. Native selector `0x14054def0` chooses mask programs from texture kind, additional-operation byte and phase. Prepare/Begin/End/Resolve/Finish at `0x1404c2910`, `0x1404c5120`, `0x1404c5260`, `0x1404c4420`, `0x1404c3150` own the two A8 targets and mask source. The original startup selects mode1.

Phase3 writes a mask; phase4 multiplies by the previous mask; phase5 subtracts through native blend row9. Additional variants apply `saturate(alpha * alphaMulAdd.x + alphaMulAdd.y)`. Resolve uses the latest mask at t1 and restores the exact original target/clip. The copied destination stays t2; masked special programs1148/1150 multiply source alpha by mask alpha before interpolation and ignore vertex color.

| Runtime indices | Readable source SHA-256 | Original/compiled container SHA-256 |
| --- | --- | --- |
| 2042,2044 | `521ef3acc30a44018d31dc61e1d3da8ce129241bc659023c1f0801e4e6fa7fc0` | `4585cc2f570773b0d06b9eb2eef8ba0f5259192a2caf96bdbd0c890e1dbaeb16` |
| 2043 | `a493713c0fa9b0435478e7feb5a32bf485d2a1f868b2fed157fc84f1e95ac50e` | `9ff397e471fbaafc309a2916f023dc624b01e8138747d121d95c2d9c89d9e2ae` |
| 2045,2047 | `b39bf96047acde9bee5c006a2a2c0aed8b7ad883d00ef3c051db93e81f7f1521` | `0213257258a9fa1af8e3bbd644b64bf409bbf84d19e4487e81886fc5d558f6d1` |
| 2046 | `8620e116994be14d776ca6ac515682d2bc0aed2bbfd66f2610d71d79398a82e2` | `b756e862f27715104e870433f291db920c2645e288225e4f20d251ecaae3e7cb` |
| 2048,2050 | `9830511dde11965610cf4c8f2401c1b8d509a4132296df26cc618c89fd448422` | `432c0475136871f87024a041af9eff6c04103ecbf626f0ebb1a20c24107238c9` |
| 2049 | `2f5075bc1751e4ebabace1155d763ef089e8bae048949cab7ec7d6b9895acb8c` | `ec8abc94614e07953dce2e609ba7fa02f8e7d06a9b7e479d5c9b19e143002ca9` |
| 2051,2053 | `3c9a95afdce58e778f036c1f7da45f3120853d1964c78e265b5f7a9552e22ef4` | `2ef5e2b30e0b5025250735775c06b5ad742dcc16bc20e1028020f93dedbb617c` |
| 2052 | `de68bd9957840ce01703e5cd4b74337519129a352da9aa241d0a5bc761ffd7ab` | `d8ce8d990132f323deaba31416149cfe0cd858723c6a94cc14c05d8fdfdf6241` |
| 2054,2056 | `5ec34e5af910ecba6a9a0c10b15841e7bee6106dd0a91b4a6840236d157e420b` | `eb4bc0fb770a1fb8e134000f0a83a8c5244ae48b0fab4783ec284671b4796bce` |
| 2055 | `5c7a01d9b41413f6dd594337c0db0a4c41bf3e112138699c52519e2ed2368c9b` | `18ce378ebd88552e370bbce53874e2d152331f34c546c9634ebeca8b635f0a76` |
| 2057,2059 | `c2fd7258a0f454283d58f68a23e60ba5cfaa7a75234228e5a9575a36a4b7e82b` | `416b26f8700e266d3e4c82fb5d19160bcc491a48a52fbc88d7073c0a63c33471` |
| 2058 | `d3b92abde4d6f2f3bf327a91bfeea11300af16d0807453e2449bd7ff32135790` | `5128a2089c3250b2dcf8815fecee37be05a12fbbc8f98231123a0c09b2f7d91a` |
| 2060,2062 | `80522b464f98ff45ab8658ae68ef0f39b55e0552f6e8f7bf282c37d5439d3607` | `865d0c970231174b4867a20f11b5d06f3c9c197c05312167d0370cf276930e74` |
| 2061 | `6f10ab7b05f541893563437a0acac1e1d7594b40c98368cd7960a4ab84d46405` | `40d2acae7651d868948ffb763b6ad3442405f3ea34d2e0ee4704a1bba857029c` |
| 2063,2065 | `ce3d68f6a7e102476621962db0dd630b414ea9944a6ba42561a4248ee92425a3` | `05bc5bd0d830386345e0d2c925d7f644c51166c93a48a2c2faf781a3453151b7` |
| 2064 | `495b3292dc277fedb1c00efedabe9414f4215a67070fcb786f5fbf7a4721fe31` | `c66ca454c52e75156ebd3abf595e2a137677d2bc966d2af757bf3eaaab654137` |
| 2066,2068 | `fdb208c70f7bc7d1f24d4219921d4bf1e02b60c8a74441cca852f4b40b2488a9` | `f4057cfea1388222c9b07b2c463dbd83bb17faa4325708eb6b5d922d19b35bfc` |
| 2067 | `ec5bd0801a51b2315459391995ad255e4ea645044d9b34f73128679c9dae835e` | `67748cb63f2a84e2600daffbdbb47077296a906110f04ed916f33439e07ee64e` |
| 2069,2071 | `76dd205398306d5732a3bbe7a7e448c5e84947b8b060392f73892b45e62ea833` | `27ce87bfdd98d7e617ea1b8a2d85bcd4202e6ce24f654f273d3a5cb0620bcf40` |
| 2070 | `f28e9c31d2c36a0feb1e61a205cdf50606c30c2121fb73dbc36a876614cb22b1` | `64d1323ee2c41efa8dd2c6b2309e7ff8fe904bd5368a46b86d1dec53ba12f2bc` |
| 1148 | `99311c75fdd40594b59fab8454bc61f4fa808092b7211beae4dfbdb7c85758e9` | `3e520f778defe4c903c729db9cea8368d729fa5c8764bfa4660edb90c4095298` |
| 1150 | `2781f6b1816126f72ed43bb4d0b986bdd0de8fa53c51e0ac81a336b2e7d47ddb` | `83fecef93098d7199f3b826318d4105087bbff6f3da5325208257fb45cd53544` |

Metal translation, target-switch lifetime, physical pixels and whole-game fidelity remain separate acceptance checks.
