# Assembly macros for splat output, written for the period assembler
# (ee-as 2.9-991111): no \() concatenation, no quoted symbol names.
# jlabel exports jump-table targets: the tables live in another object.
.macro glabel label, visibility=global
    .\visibility \label
    .type \label, @function
    \label:
.endm

.macro endlabel label
    .size \label, . - \label
.endm

# alabel marks a label inside a function (a jump target or an inline table),
# not a function of its own
.macro alabel label, visibility=global
    .\visibility \label
    \label:
.endm

.macro ehlabel label, visibility=global
    \label:
.endm

.macro jlabel label, visibility=global
    .\visibility \label
    \label:
.endm

.macro dlabel label, visibility=global
    .\visibility \label
    .type \label, @object
    \label:
.endm

.macro enddlabel label
    .size \label, . - \label
.endm

.macro nonmatching label, size=1
.endm

