all:
	pdflatex -interaction=nonstopmode main.tex >/dev/null || true
	bibtex main >/dev/null || true
	pdflatex -interaction=nonstopmode main.tex >/dev/null || true
	pdflatex -interaction=nonstopmode main.tex

clean:
	rm -f main.aux main.bbl main.blg main.log main.out main.pdf
