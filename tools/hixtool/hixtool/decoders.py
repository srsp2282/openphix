"""Decoders for the individual files inside the image. Each decoder module
registers its own sub commands here."""
from . import cmd, exp, feedback, fonts, images, strings


def register(sub):
    strings.register(sub)
    exp.register(sub)
    cmd.register(sub)
    fonts.register(sub)
    images.register(sub)
    feedback.register(sub)
