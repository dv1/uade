
all:	design.ps

design.ps:	design.dvi
	dvips -o $@ $<

design.dvi:	design.tex
	texi2dvi -c $<
