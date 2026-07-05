all: main.pdf main-ieee.pdf

main.pdf: main.tex body.tex preamble-common.tex refs.bib
	pdflatex -interaction=nonstopmode main.tex >/dev/null || true
	bibtex main >/dev/null || true
	pdflatex -interaction=nonstopmode main.tex >/dev/null || true
	pdflatex -interaction=nonstopmode main.tex | tail -2

main-ieee.pdf: main-ieee.tex body.tex preamble-common.tex refs.bib IEEEtran.cls IEEEtran.bst
	pdflatex -interaction=nonstopmode main-ieee.tex >/dev/null || true
	bibtex main-ieee >/dev/null || true
	pdflatex -interaction=nonstopmode main-ieee.tex >/dev/null || true
	pdflatex -interaction=nonstopmode main-ieee.tex | tail -2

clean:
	rm -f main*.aux main*.bbl main*.blg main*.log main*.out main*.pdf
