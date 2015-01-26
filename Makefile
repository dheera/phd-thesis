chapters = chapter-pcoct chapter-ghost chapter-fpi
main = thesis
.PHONY: $(chapters) show

show: $(main).pdf
    open $<

$(main).pdf: $(main).tex $(addsuffix .tex,$(chapters))
    pdflatex --shell-escape $(main)

$(chapters): %: ch-%.pdf
    open $<

ch-%.pdf: %.tex
    pdflatex --shell-escape --jobname=ch-$* "\includeonly{$*}\input{$(main)}"
