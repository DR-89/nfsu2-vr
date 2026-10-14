package org.nfsu2.quest;

/** Metadata only: the supported, unmodified Xbox NTSC-U disc. No game assets. */
final class GameManifest {
    static final GameDataImporter.Expected[] FILES = {
        new GameDataImporter.Expected("default.xbe", 4038656L, "c8cab1bfe7cf26553e84eb4ef2a26d16f6b06260f7494c583802bc00fd81dd31"),
        new GameDataImporter.Expected("NFSUNDER/ZDIR.BIN", 32688L, "254212625c80eb720daf139290b8d19aa159cb6890bebc75d11c2d3da52fb722"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA0.BIN", 235780096L, "8cc13a0f9e31107bc09706244cccf0ffb9167009a6c81069b148f5814bb1fa03"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA1.BIN", 242006016L, "7076168a39fe70e1e7eda8b1bd8aaceab7e45385113d4cee83dc138b6aad0b86"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA2.BIN", 248778752L, "5f85d28278aa0496af7402c2753af5d42f2dc30a7b239a2b3c04742139646005"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA3.BIN", 245807104L, "cdb1d3b61381d84685442c027f423bc32b3e869862b9588b5ccf8d2bf1247c84"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA4.BIN", 243816448L, "489bbdfa18765bd6132da59c83895672a0c313f33f38755887c4f9560676a9de"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA5.BIN", 244525056L, "35bd6d5877b99912e68e31b853f317175954e6e8c3c5a2fabbe699395729d1c5"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA6.BIN", 170592256L, "6afda5632d384319c073de022021a02bd2b9972663910ad50d859cf9015ab209"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA7.BIN", 110309376L, "74a5001a9fe6de61f5b8bb910e11400bdd11982b254cccd708d8711be9d6578b"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA8.BIN", 296962048L, "e421b945bdffd78ac8b8219d42a9d8c24045d646a4f9585d7cc028f3791f9e06"),
        new GameDataImporter.Expected("NFSUNDER/ZZDATA9.BIN", 396361728L, "58196c8a080dcf428ca4cb202a53b207bc3bab3edabd708cf28b6d9db453a09f")
    };
    private GameManifest() {}
}
