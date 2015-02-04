TEX = pdflatex -shell-escape -interaction=nonstopmode -file-line-error
PRE =  $(TEX) -ini -job-name="preamble" "&pdflatex preamble.tex\dump"
BIB = bibtex
VIEWER = evince

.PHONY: all view

all : thesis

view :
	$(VIEWER) thesis.pdf &

thesis : thesis.tex thesis.bbl thesis.blg
	$(TEX) thesis.tex && $(TEX) thesis.tex

thesis.bbl thesis.blg : thesis.bib thesis.aux
	$(BIB) thesis

thesis.aux : thesis.tex
	$(TEX) thesis.tex

thesis.bib : thesis.tex
	$(TEX) thesis.tex

