EdgeReflex: Schedulable Agentic Inference on Constrained Embedded Hardware via Hardware-Software Co-Design

================================================================================
COMPILATION INSTRUCTIONS
================================================================================

This is a complete IEEE conference paper in LaTeX format. To compile the paper
into a PDF, follow the steps below.

PREREQUISITES
=============

You must have a LaTeX distribution installed on your system:

  - Linux/Mac: Install TeX Live (full)
    $ sudo apt-get install texlive-full        (Ubuntu/Debian)
    $ brew install basictex                     (macOS with Homebrew)
  
  - Windows: Install MiKTeX or TeX Live
    Download from https://miktex.org or https://www.tug.org/texlive/

COMPILATION
===========

Navigate to the paper directory and run:

  $ pdflatex main.tex
  $ bibtex main
  $ pdflatex main.tex
  $ pdflatex main.tex

The first pdflatex pass creates an auxiliary file (main.aux) that bibtex uses
to generate the bibliography. The next two pdflatex passes resolve cross-references
and ensure all citations are properly resolved.

Output
------

After successful compilation, you will have:

  - main.pdf             (compiled PDF document)
  - main.aux             (auxiliary file)
  - main.bbl             (bibliography file)
  - main.blg             (bibliography log)
  - main.log             (compilation log)

ALTERNATIVE: One-Line Compilation (Linux/Mac)
==============================================

You can use a single command to automate the entire process:

  $ pdflatex main.tex && bibtex main && pdflatex main.tex && pdflatex main.tex

TROUBLESHOOTING
===============

1. "IEEEtran.cls not found"
   - Ensure IEEEtran.cls is in the same directory as main.tex.
   - If using a system-wide LaTeX installation, the .cls file may already be
     available; try placing all files in a single directory and recompiling.

2. Bibliography not appearing
   - Ensure references.bib is in the same directory as main.tex.
   - Run the full sequence: pdflatex → bibtex → pdflatex → pdflatex.
   - Check main.blg for bibtex errors.

3. "undefined control sequences" or package errors
   - Ensure you have a complete LaTeX installation with graphicx, amsmath,
     pgfplots, tikz, and booktabs packages.
   - On Linux: $ sudo apt-get install texlive-full
   - On macOS: $ brew install --cask mactex

4. PDF not generated
   - Check main.log for errors.
   - Verify that all .tex and .bib files have Unix-style line endings (LF),
     not Windows (CRLF).

FILE STRUCTURE
==============

paper/
  main.tex              - Main LaTeX document (all content)
  references.bib        - Bibliography file (IEEE-formatted citations)
  IEEEtran.cls          - IEEE conference document class
  README.txt            - This file
  main.pdf              - Output (generated after compilation)

PAPER STRUCTURE
===============

The paper contains the following sections:

  I.   Introduction
  II.  Related Work
  III. System Design
       A. Hardware Platform
       B. Task Model
       C. WCET Measurement Methodology
       D. MLP Model
       E. Agentic State Extension
       F. FPGA Accelerator Design
  IV.  Results
       A. Baseline Schedulability (Stateless, 1000 ms)
       B. Agentic Loop Crisis (100 ms, Five Tasks)
       C. FPGA Offload Projected Results
       D. WCET Distribution and Determinism
  V.   Discussion
  VI.  Conclusion

References and Keywords

FIGURES AND TABLES
==================

All tables are embedded inline in the LaTeX source. Figures are generated
using TikZ and pgfplots; no external image files are required.

MODIFYING THE PAPER
===================

To edit the paper:

1. Open main.tex in any text editor (or LaTeX IDE like TeXShop, TeXStudio, Overleaf).
2. Modify the content as needed.
3. Recompile using the commands above.

To add or modify citations:

1. Edit references.bib to add new bibliography entries.
2. Use \cite{key} in the main text to reference them.
3. Recompile with the full pdflatex → bibtex → pdflatex → pdflatex sequence.

LICENSE AND ATTRIBUTION
=======================

This paper and all associated code (firmware, FPGA HDL, analysis scripts)
are provided as part of the EdgeReflex project. Please cite as:

  [Author Name], "EdgeReflex: Schedulable Agentic Inference on Constrained
  Embedded Hardware via Hardware-Software Co-Design," IEEE Conference, 2026.

CONTACT
=======

For questions or issues, please refer to the EdgeReflex project repository.

Last updated: 2026-05-08
