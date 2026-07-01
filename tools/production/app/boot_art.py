"""
boot_art.py — ASCII-логотип компании для WaitingScreen.

Сгенерирован программно (coverage-based растеризация logo.png в
символы плотности + разделение по цвету blue/dark), затем зафиксирован
как статичная строка — рантайм-зависимости на Pillow/etc. не требуется.
"""

from __future__ import annotations

LOGO_ART = """
[grey37]........[/grey37][#3ca0dc].**:+*::*+.[/#3ca0dc][grey37]..........[/grey37]
[grey37]........[/grey37][#3ca0dc].##:*#++#*.[/#3ca0dc][grey37]..........[/grey37]
[grey37].........[/grey37][#3ca0dc]*#[/#3ca0dc][grey37].[/grey37][#3ca0dc]:#..#+[/#3ca0dc][grey37]...........[/grey37]
[grey37].......[/grey37][#3ca0dc].+#:[/#3ca0dc][grey37].[/grey37][#3ca0dc]:#..#+[/#3ca0dc][grey37]...........[/grey37]
[grey37].....[/grey37][#3ca0dc].+#+..:#*..#+[/#3ca0dc][grey37]...........[/grey37]
[grey37]...[/grey37][#3ca0dc].+#+..:**:..*#.[/#3ca0dc][white].:.[/white][grey37]........[/grey37]
[grey37].[/grey37][#3ca0dc].+#+..:#*:..*#+.[/#3ca0dc][white]+@@+[/white][grey37]........[/grey37]
[grey37].[/grey37][#3ca0dc]##..:#*.[/#3ca0dc][grey37].[/grey37][#3ca0dc].*#+.[/#3ca0dc][white]%@@@@%[/white][grey37].[/grey37][white]::.[/white][grey37]....[/grey37]
[grey37].[/grey37][#3ca0dc]#*[/#3ca0dc][grey37].[/grey37][#3ca0dc]:#:[/#3ca0dc][grey37].[/grey37][#3ca0dc].*#+.[/#3ca0dc][white]+@@@@@@%[/white][grey37].[/grey37][white]+@...[/white][grey37]..[/grey37]
[grey37].[/grey37][#3ca0dc]#*[/#3ca0dc][grey37].[/grey37][#3ca0dc]+#..#+.[/#3ca0dc][grey37]........[/grey37][white]%%[/white][grey37].[/grey37][white]+@..%.[/white][grey37].[/grey37]
[grey37].[/grey37][#3ca0dc]#*[/#3ca0dc][grey37].[/grey37][#3ca0dc]+#.:#:[/#3ca0dc][grey37].........[/grey37][white]%%[/white][grey37].[/grey37][white]+@..@:[/white][grey37].[/grey37]
[grey37].[/grey37][#3ca0dc]**[/#3ca0dc][grey37].[/grey37][#3ca0dc]+#.:#:[/#3ca0dc][grey37]........[/grey37][white].%%[/white][grey37].[/grey37][white]+@..@:[/white][grey37].[/grey37]
[grey37].[/grey37][#3ca0dc]..[/#3ca0dc][grey37].[/grey37][#3ca0dc]+#.:#*+++++.[/#3ca0dc][white].:@%.[/white][grey37].[/grey37][white]+@..@:[/white][grey37].[/grey37]
[grey37]....[/grey37][#3ca0dc].*.:#####+.[/#3ca0dc][white]+@%.[/white][grey37].[/grey37][white]:%%:[/white][grey37].[/grey37][white]:@:[/white][grey37].[/grey37]
[grey37]......[/grey37][#3ca0dc].:###+.[/#3ca0dc][white]+@%.[/white][grey37].[/grey37][white].%%:..+@+.[/white][grey37].[/grey37]
[grey37].......[/grey37][#3ca0dc].:+.[/#3ca0dc][white]:@%.[/white][grey37].[/grey37][white].%@:..+@+.[/white][grey37]...[/grey37]
[grey37]...........[/grey37][white]@%[/white][grey37].[/grey37][white].%@:..+@+.[/white][grey37].....[/grey37]
[grey37]...........[/grey37][white]@%[/white][grey37].[/grey37][white]%@..+@+.[/white][grey37].......[/grey37]
[grey37]...........[/grey37][white]@%[/white][grey37].[/grey37][white]%@[/white][grey37].[/grey37][white]:@:[/white][grey37].........[/grey37]
[grey37]..........[/grey37][white].@%.%@.+@+[/white][grey37].........[/grey37]
[grey37]..........[/grey37][white]:@@:@@+%@%.[/white][grey37]........[/grey37]
""".strip("\n")
