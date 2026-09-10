"""Decoders for the individual files inside the image. Each decoder module
registers its own sub commands here."""
from . import cmd, exp, feedback, fonts, funcfg, images, menu, pairs, spfunc, strings, sysscan


def register(sub):
    strings.register(sub)
    exp.register(sub)
    cmd.register(sub)
    menu.register(sub)
    funcfg.register(sub)
    sysscan.register(sub)
    pairs.register(sub)
    spfunc.register(sub)
    fonts.register(sub)
    images.register(sub)
    feedback.register(sub)
