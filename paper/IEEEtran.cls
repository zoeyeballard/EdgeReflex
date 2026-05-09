%% IEEEtran.cls 2015/08/26 version V1.8b
%% This is a modified and simplified version for conference papers
%% Based on the official IEEE template

\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{IEEEtran}[2015/08/26 v1.8b IEEE Transactions]

\newif\if@conference
\newif\if@publish
\@conferencetrue

\DeclareOption{conference}{\@conferencetrue}
\DeclareOption{journal}{\@conferencefalse}
\DeclareOption{peerreview}{\@publishfalse}
\DeclareOption{publish}{\@publishtrue}

\ProcessOptions\relax

\LoadClass[twocolumn,10pt]{article}

\usepackage{ifthen}
\usepackage{calc}
\usepackage{url}

\setlength{\textheight}{9.25in}
\setlength{\textwidth}{7in}
\setlength{\topmargin}{-0.5in}
\setlength{\oddsidemargin}{-0.25in}
\setlength{\evensidemargin}{-0.25in}
\setlength{\parindent}{1em}
\setlength{\parskip}{0pt}

\renewcommand{\normalsize}{\fontsize{10}{12}\selectfont}
\renewcommand{\small}{\fontsize{9}{11}\selectfont}
\renewcommand{\footnotesize}{\fontsize{8}{10}\selectfont}
\renewcommand{\scriptsize}{\fontsize{7}{8}\selectfont}
\renewcommand{\tiny}{\fontsize{6}{7}\selectfont}
\renewcommand{\large}{\fontsize{11}{13}\selectfont}
\renewcommand{\Large}{\fontsize{13}{15}\selectfont}
\renewcommand{\LARGE}{\fontsize{16}{18}\selectfont}
\renewcommand{\huge}{\fontsize{20}{22}\selectfont}
\renewcommand{\Huge}{\fontsize{24}{26}\selectfont}

\setlength{\headsep}{0.5in}
\setlength{\headheight}{0.2in}

\def\maketitle{%
  \par
  \begingroup
    \def\thefootnote{\fnsymbol{footnote}}%
    \def\@makefnmark{\hbox to 0pt{$^{\@thefnmark}$\hss}}%
    \twocolumn[\@maketitlehw]%
    \thispagestyle{plain}%
  \endgroup
  \setcounter{footnote}{0}%
  \let\maketitle\relax
  \let\@maketitle\relax
}

\def\@maketitlehw{%
  \newpage
  \null
  \vskip 2em%
  \begin{center}%
    {\LARGE\bfseries\@title\par}%
    \vskip 1.5em%
    {\large
      \lineskip .5em%
      \begin{tabular}[t]{c}%
        \@author
      \end{tabular}\par}%
  \end{center}%
  \par
  \vskip 1.5em}

\renewenvironment{abstract}{%
  \section*{Abstract}
}{%
  \par
}

\newenvironment{IEEEkeywords}{%
  \par\noindent\textbf{Index Terms---}%
}{%
  \par
}

\newcommand{\IEEEoverridecommandlockouts}{}

\renewcommand\section{\@startsection {section}{1}{\z@}%
  {-3.5ex \@plus -1ex \@minus -.2ex}%
  {2.3ex \@plus.2ex}%
  {\normalfont\large\bfseries}}

\renewcommand\subsection{\@startsection{subsection}{2}{\z@}%
  {-3.25ex\@plus -1ex \@minus -.2ex}%
  {1.5ex \@plus .2ex}%
  {\normalfont\normalsize\bfseries}}

\renewcommand\subsubsection{\@startsection{subsubsection}{3}{\z@}%
  {-3.25ex\@plus -1ex \@minus -.2ex}%
  {1.5ex \@plus .2ex}%
  {\normalfont\normalsize\itshape}}

\renewcommand\paragraph{\@startsection{paragraph}{4}{\z@}%
  {3.25ex \@plus1ex \@minus.2ex}%
  {-1em}%
  {\normalfont\normalsize\bfseries}}

\renewcommand\subparagraph{\@startsection{subparagraph}{5}{\parindent}%
  {3.25ex \@plus1ex \@minus .2ex}%
  {-1em}%
  {\normalfont\normalsize\itshape}}

\setcounter{secnumdepth}{4}

\renewcommand{\thesection}{\Roman{section}}
\renewcommand{\thesubsection}{\Alph{subsection}}
\renewcommand{\thesubsubsection}{\arabic{subsubsection}}



\pagestyle{plain}

\renewcommand{\@cite}[2]{[#1\if@tempswa , #2\fi]}

\DeclareRobustCommand{\VAN}[3]{#3}

\endinput
