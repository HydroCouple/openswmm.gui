// Configure MathJax 3 to recognize $...$ (inline) and $$...$$ (display)
// delimiters used in GitHub-flavored Markdown files (README.md, etc.)
// that Doxygen processes via USE_MDFILE_AS_MAINPAGE.
window.MathJax = {
  tex: {
    inlineMath:  [['$', '$'], ['\\(', '\\)']],
    displayMath: [['$$', '$$'], ['\\[', '\\]']]
  }
};
