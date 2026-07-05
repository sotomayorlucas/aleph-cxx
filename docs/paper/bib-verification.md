# Bibliography verification

Verified against arXiv abstract pages, dblp, Crossref (`api.crossref.org`), publisher pages, and authors' own bibtex pages. Each item gives a verdict (CONFIRMED / CORRECTED / UNVERIFIABLE), the authoritative record, and the URL(s) checked. A complete corrected BibTeX body is at the end.

---

## 1. arXiv:2208.09535 — discretized Ricci curvatures of graphs

**Verdict: CORRECTED (authors were wrong; now also has a journal home).**

The guessed authors "Ni, Chien-Chun and Wang, Hao" are **wrong**. The real authors are **Bhaskar DasGupta, Elena Grigorescu, Tamalika Mukherjee**.

- Title (exact): *On computing Discretized Ricci curvatures of graphs: local algorithms and (localized) fine-grained reductions*
- arXiv: 2208.09535, submitted 19 Aug 2022 (v3, 11 Aug 2023).
- **Journal version:** it did appear in *Theoretical Computer Science*, **volume 975, article 114127 (2023)**, DOI **10.1016/j.tcs.2023.114127**.
- Verified: https://arxiv.org/abs/2208.09535 , https://www.sciencedirect.com/science/article/abs/pii/S0304397523004401 , DasGupta publication list.

Note: the arXiv page's DOI field only shows the arXiv DOI; the TCS volume/article/year comes from the ScienceDirect record (S0304397523004401) and the authors' publication list. The TCS DOI 10.1016/j.tcs.2023.114127 corresponds to article number 114127 in vol. 975.

---

## 2. arXiv:2603.11060 — Lin–Lu–Yau Ricci reweighting in the SBM

**Verdict: CORRECTED (exact title and author established).**

- Title (exact, arXiv metadata): *LLY Ricci Reweighting in Stochastic Block Models: Uniform Curvature Concentration and Finite-Horizon Tracking*
- **Author: Varun Kotharkar (single author).**
- arXiv: 2603.11060 [cs.SI], submitted 4 Mar 2026, v1.
- Verified: https://arxiv.org/abs/2603.11060

Caveat: the HTML full-text renders the title as "Lin–Lu–Yau Ricci Reweighting in the SBM: Uniform Curvature Concentration and Finite-Horizon Tracking"; the abstract-page metadata (authoritative for citation) uses "LLY Ricci Reweighting in Stochastic Block Models: …". Use the metadata form.

---

## 3. Herholz & Alexa, "Factor Once" (TOG 2018)

**Verdict: CONFIRMED, with article number added.**

- Philipp Herholz, Marc Alexa, *Factor Once: Reusing Cholesky Factorizations on Sub-Meshes*.
- ACM Transactions on Graphics **37(6)**, Article **230** (2018), 9 pp. (SIGGRAPH Asia 2018).
- DOI **10.1145/3272127.3275107**.
- Verified: https://dblp.org/rec/journals/tog/HerholzA18.html , https://phherholz.github.io/bibtex.html , https://dl.acm.org/doi/10.1145/3272127.3275107

---

## 4. Herholz, Davis, Alexa, "Localized solutions…" (TOG 2017)

**Verdict: CONFIRMED. Timothy A. Davis is correct.**

- Philipp Herholz, **Timothy A. Davis**, Marc Alexa, *Localized solutions of sparse linear systems for geometry processing*.
- ACM Transactions on Graphics **36(6)**, Article **183** (2017), 8 pp. (SIGGRAPH Asia 2017).
- DOI **10.1145/3130800.3130849**.
- Verified: https://api.crossref.org/works/10.1145/3130800.3130849 (authors incl. Timothy A. Davis, vol 36, issue 6, 2017), https://phherholz.github.io/bibtex.html , https://dl.acm.org/doi/10.1145/3130800.3130849 (Article 183).

---

## 5. Herholz & Sorkine-Hornung, "Sparse Cholesky Updates…" (TOG 2020)

**Verdict: CONFIRMED, with article number added.**

- Philipp Herholz, Olga Sorkine-Hornung, *Sparse Cholesky Updates for Interactive Mesh Parameterization*.
- ACM Transactions on Graphics **39(6)**, Article **202** (202:1–202:14) (2020), 14 pp. (SIGGRAPH Asia 2020).
- DOI **10.1145/3414685.3417828**.
- Verified: https://dblp.org/rec/journals/tog/HerholzS20.html , https://dl.acm.org/doi/10.1145/3414685.3417828

---

## 6. Durfee, Gao, Goranci, Peng — "Fully Dynamic Spectral Vertex Sparsifiers and Applications"

**Verdict: CORRECTED (arXiv number was wrong; venue is STOC 2019).**

The guessed arXiv id **1909.06413 is wrong** — that id is Gramoz Goranci's PhD thesis *"Dynamic Graph Algorithms and Graph Sparsification: New Techniques and Connections"* (submitted 13 Sep 2019).

The intended paper:
- David Durfee, Yu Gao, Gramoz Goranci, Richard Peng, *Fully Dynamic Spectral Vertex Sparsifiers and Applications*.
- **arXiv: 1906.10530** (submitted 24 Jun 2019; "STOC 2019" in comments).
- **Venue: STOC 2019** — Proceedings of the 51st Annual ACM SIGACT Symposium on Theory of Computing, **pp. 914–925**, ACM, 2019.
- DOI **10.1145/3313276.3316379**.
- Verified: https://arxiv.org/abs/1909.06413 (thesis), https://arxiv.org/abs/1906.10530 , https://api.crossref.org/works/10.1145/3313276.3316379 (pages 914–925), https://dl.acm.org/doi/10.1145/3313276.3316379

---

## 7. Jerboa (ICGT 2014)

**Verdict: CONFIRMED; LNCS volume is 8571 (not the 8468 that Crossref's `volume` field reports).**

- Hakim Belhaouari, Agnès Arnould, Pascale Le Gall, Thomas Bellet, *Jerboa: A Graph Transformation Library for Topology-Based Geometric Modeling*.
- In *Graph Transformation — 7th International Conference, ICGT 2014, Held as Part of STAF 2014, York, UK, July 22–24, 2014. Proceedings* (eds. Holger Giese, Barbara König).
- Lecture Notes in Computer Science **8571**, **pp. 269–284**, Springer, 2014.
- DOI **10.1007/978-3-319-09108-2_18**.
- Verified: https://dblp.org/rec/conf/gg/BelhaouariAGB14.bib (series = LNCS, volume = 8571), https://hal.science/hal-01012851/ . (Crossref's JSON `volume` field returned 8468, which is incorrect for this book; dblp's 8571 is authoritative.)

---

## 8. Glenn Fiedler, "Floating Point Determinism" (Gaffer On Games)

**Verdict: CONFIRMED. Year = 2010.**

- Glenn Fiedler, *Floating Point Determinism*, Gaffer On Games, **24 February 2010**.
- URL: https://gafferongames.com/post/floating_point_determinism/ (dateline "Feb 24, 2010" on the page).

---

## 9. Demmel & Nguyen — reproducible summation / ReproBLAS

**Verdict: CONFIRMED. There are two canonical citations (conference + journal).**

Conference (the one usually cited for "fast reproducible summation"):
- James Demmel, Hong Diep Nguyen, *Fast Reproducible Floating-Point Summation*.
- 2013 IEEE 21st Symposium on Computer Arithmetic (ARITH 2013), **pp. 163–172**, IEEE, 2013.
- DOI **10.1109/ARITH.2013.9**.

Journal (the extended, more complete reference — note lead author is Ahrens, not Demmel):
- **Willow Ahrens**, James Demmel, Hong Diep Nguyen, *Algorithms for Efficient Reproducible Floating Point Summation*.
- ACM Transactions on Mathematical Software **46(3)**, Article **22** (2020), 49 pp.
- DOI **10.1145/3389360**.
- Verified: https://api.crossref.org/works/10.1145/3389360 , https://dl.acm.org/doi/10.1145/3389360 , https://bebop.cs.berkeley.edu/reproblas/ , ARITH 2013 record via 10.1109/ARITH.2013.9.

Recommendation: cite the ARITH 2013 paper for the core algorithm; add the TOMS 2020 paper if you cite ReproBLAS as software.

---

## 10. arXiv:2606.04227 — Volk 2026, incremental sheaf cohomology

**Verdict: CONFIRMED.**

- **Jason L. Volk** (single author), *Incremental Sheaf Cohomology on Cellular Complexes: O(1)-in-n Lazy Edit Processing under Bounded Local Geometry*.
- arXiv: 2606.04227 [cs.DS] (cross-list cs.AI), submitted 2 Jun 2026; **current version v2, 6 Jun 2026**.
- Verified: https://arxiv.org/abs/2606.04227

---

## 11. arXiv:2603.27015 — Halley Young, sheaf-cohomological program analysis

**Verdict: CONFIRMED.**

- **Halley Young**, *Sheaf-Cohomological Program Analysis: Unifying Bug Finding, Equivalence, and Verification via Čech Cohomology*.
- arXiv: 2603.27015 [cs.PL], submitted 27 Mar 2026, v1.
- Verified: https://arxiv.org/abs/2603.27015 (arXiv id resolves to this exact title), Microsoft Research publication page.

---

## 12. NEW — physics engine for 3D Gaussian splatting (shared representation claim)

**Verdict: Use arXiv:2606.21753. It fits the claim; 2606.00444 does not.**

- **Xiaoyang Liu, Shangzhe Wu, Kai Han**, *Scene-Level Heterogeneous Physics Simulation with 3D Gaussian Splats*.
- arXiv: 2606.21753, submitted 19 Jun 2026.
- **Exact supporting sentences (quoted from the abstract):**
  - "3D Gaussian Splatting (3DGS) has achieved state-of-the-art photorealistic rendering, but the representation gap prevents these assets from being physically interactive."
  - "**Production-grade physics engines do not understand the 3DGS representation, while prior physics-for-3DGS methods are monolithic silos.**"
- Verified: https://arxiv.org/abs/2606.21753

The other candidate, **arXiv:2606.00444** (Adrian Ramlal, John S. Zelek, *Real-Time Physics Simulation with Dynamic Mesh-Gaussian Reconstructions*, 30 May 2026), is about fixed- vs. varying-topology mesh conversion for DG-Mesh; its abstract contains **no** statement about physics engines being representation-agnostic or lacking a shared native representation. So 2606.21753 is the better fit for the claim "physics and rendering lack a shared native representation."

---

## Unflagged entries — quick check

All confirmed correct as stated (spot-checked against dblp/Crossref/publisher pages):

| Entry | Status |
|---|---|
| Ollivier 2009, J. Funct. Anal. **256(3):810–864** | CONFIRMED (DOI 10.1016/j.jfa.2008.11.001) |
| Jost & Liu 2014, Discrete Comput. Geom. **51(2):300–322** | CONFIRMED (DOI 10.1007/s00454-013-9558-1) |
| Ni-Lin-Luo-Gao 2019, Sci. Rep. **9:9984** | CONFIRMED (DOI 10.1038/s41598-019-46380-9) |
| Hansen & Ghrist 2019, J. Appl. Comput. Topol. **3:315–358** | CONFIRMED (DOI 10.1007/s41468-019-00038-7) |
| Crane-Weischedel-Wardetzky, TOG **32(5)** 2013 | CONFIRMED (Article 152, DOI 10.1145/2516971.2516977) |
| Sharp-Soliman-Crane, TOG **38(3)** 2019 | CONFIRMED (Article 24, DOI 10.1145/3243651) |
| Sorkine et al., SGP 2004 | CONFIRMED (Laplacian Surface Editing, pp. 175–184, DOI 10.1145/1057432.1057456) |
| Bouaziz et al., TOG **33(4)** 2014 | CONFIRMED (Article 154, DOI 10.1145/2601097.2601116) |
| Elcott et al., TOG **26(1)** 2007 | CONFIRMED (Article 4, DOI 10.1145/1189762.1189766) |
| Desbrun-Hirani-Leok-Marsden, arXiv:math/0508341, 2005 | CONFIRMED |
| Ehrig et al., Springer 2006 | CONFIRMED (*Fundamentals of Algebraic Graph Transformation*, EATCS Monographs, DOI 10.1007/3-540-31188-2) |

---

## Corrected BibTeX (ready to paste)

```bibtex
@article{dasgupta2023ricci,
  author  = {DasGupta, Bhaskar and Grigorescu, Elena and Mukherjee, Tamalika},
  title   = {On computing Discretized {Ricci} curvatures of graphs: local algorithms and (localized) fine-grained reductions},
  journal = {Theoretical Computer Science},
  volume  = {975},
  pages   = {114127},
  year    = {2023},
  doi     = {10.1016/j.tcs.2023.114127}
}

@misc{kotharkar2026lly,
  author = {Kotharkar, Varun},
  title  = {{LLY} Ricci Reweighting in Stochastic Block Models: Uniform Curvature Concentration and Finite-Horizon Tracking},
  year   = {2026},
  eprint = {2603.11060},
  archivePrefix = {arXiv},
  primaryClass  = {cs.SI},
  note   = {arXiv:2603.11060}
}

@article{herholz2018factor,
  author  = {Herholz, Philipp and Alexa, Marc},
  title   = {Factor Once: Reusing {Cholesky} Factorizations on Sub-Meshes},
  journal = {ACM Transactions on Graphics},
  volume  = {37},
  number  = {6},
  articleno = {230},
  pages   = {230:1--230:9},
  year    = {2018},
  doi     = {10.1145/3272127.3275107}
}

@article{herholz2017localized,
  author  = {Herholz, Philipp and Davis, Timothy A. and Alexa, Marc},
  title   = {Localized solutions of sparse linear systems for geometry processing},
  journal = {ACM Transactions on Graphics},
  volume  = {36},
  number  = {6},
  articleno = {183},
  pages   = {183:1--183:8},
  year    = {2017},
  doi     = {10.1145/3130800.3130849}
}

@article{herholz2020sparse,
  author  = {Herholz, Philipp and Sorkine-Hornung, Olga},
  title   = {Sparse {Cholesky} Updates for Interactive Mesh Parameterization},
  journal = {ACM Transactions on Graphics},
  volume  = {39},
  number  = {6},
  articleno = {202},
  pages   = {202:1--202:14},
  year    = {2020},
  doi     = {10.1145/3414685.3417828}
}

@inproceedings{durfee2019fully,
  author    = {Durfee, David and Gao, Yu and Goranci, Gramoz and Peng, Richard},
  title     = {Fully Dynamic Spectral Vertex Sparsifiers and Applications},
  booktitle = {Proceedings of the 51st Annual ACM SIGACT Symposium on Theory of Computing (STOC 2019)},
  pages     = {914--925},
  year      = {2019},
  publisher = {ACM},
  doi       = {10.1145/3313276.3316379}
}

@inproceedings{belhaouari2014jerboa,
  author    = {Belhaouari, Hakim and Arnould, Agn{\`e}s and Le Gall, Pascale and Bellet, Thomas},
  title     = {Jerboa: A Graph Transformation Library for Topology-Based Geometric Modeling},
  booktitle = {Graph Transformation --- 7th International Conference, ICGT 2014, Held as Part of STAF 2014, York, UK, July 22--24, 2014. Proceedings},
  editor    = {Giese, Holger and K{\"o}nig, Barbara},
  series    = {Lecture Notes in Computer Science},
  volume    = {8571},
  pages     = {269--284},
  year      = {2014},
  publisher = {Springer},
  doi       = {10.1007/978-3-319-09108-2_18}
}

@misc{fiedler2010floating,
  author = {Fiedler, Glenn},
  title  = {Floating Point Determinism},
  howpublished = {Gaffer On Games},
  year   = {2010},
  month  = feb,
  note   = {\url{https://gafferongames.com/post/floating_point_determinism/}}
}

@inproceedings{demmel2013fast,
  author    = {Demmel, James and Nguyen, Hong Diep},
  title     = {Fast Reproducible Floating-Point Summation},
  booktitle = {2013 IEEE 21st Symposium on Computer Arithmetic (ARITH)},
  pages     = {163--172},
  year      = {2013},
  publisher = {IEEE},
  doi       = {10.1109/ARITH.2013.9}
}

@article{ahrens2020algorithms,
  author  = {Ahrens, Willow and Demmel, James and Nguyen, Hong Diep},
  title   = {Algorithms for Efficient Reproducible Floating Point Summation},
  journal = {ACM Transactions on Mathematical Software},
  volume  = {46},
  number  = {3},
  articleno = {22},
  pages   = {22:1--22:49},
  year    = {2020},
  doi     = {10.1145/3389360}
}

@misc{volk2026incremental,
  author = {Volk, Jason L.},
  title  = {Incremental Sheaf Cohomology on Cellular Complexes: {$O(1)$}-in-$n$ Lazy Edit Processing under Bounded Local Geometry},
  year   = {2026},
  eprint = {2606.04227},
  archivePrefix = {arXiv},
  primaryClass  = {cs.DS},
  note   = {arXiv:2606.04227}
}

@misc{young2026sheaf,
  author = {Young, Halley},
  title  = {Sheaf-Cohomological Program Analysis: Unifying Bug Finding, Equivalence, and Verification via {\v{C}}ech Cohomology},
  year   = {2026},
  eprint = {2603.27015},
  archivePrefix = {arXiv},
  primaryClass  = {cs.PL},
  note   = {arXiv:2603.27015}
}

@misc{liu2026scene,
  author = {Liu, Xiaoyang and Wu, Shangzhe and Han, Kai},
  title  = {Scene-Level Heterogeneous Physics Simulation with 3D Gaussian Splats},
  year   = {2026},
  eprint = {2606.21753},
  archivePrefix = {arXiv},
  primaryClass  = {cs.GR},
  note   = {arXiv:2606.21753}
}

@article{ollivier2009ricci,
  author  = {Ollivier, Yann},
  title   = {Ricci curvature of {Markov} chains on metric spaces},
  journal = {Journal of Functional Analysis},
  volume  = {256},
  number  = {3},
  pages   = {810--864},
  year    = {2009},
  doi     = {10.1016/j.jfa.2008.11.001}
}

@article{jost2014ollivier,
  author  = {Jost, J{\"u}rgen and Liu, Shiping},
  title   = {Ollivier's {Ricci} Curvature, Local Clustering and Curvature-Dimension Inequalities on Graphs},
  journal = {Discrete \& Computational Geometry},
  volume  = {51},
  number  = {2},
  pages   = {300--322},
  year    = {2014},
  doi     = {10.1007/s00454-013-9558-1}
}

@article{ni2019community,
  author  = {Ni, Chien-Chun and Lin, Yu-Yao and Luo, Feng and Gao, Jie},
  title   = {Community Detection on Networks with {Ricci} Flow},
  journal = {Scientific Reports},
  volume  = {9},
  articleno = {9984},
  year    = {2019},
  doi     = {10.1038/s41598-019-46380-9}
}

@article{hansen2019spectral,
  author  = {Hansen, Jakob and Ghrist, Robert},
  title   = {Toward a Spectral Theory of Cellular Sheaves},
  journal = {Journal of Applied and Computational Topology},
  volume  = {3},
  pages   = {315--358},
  year    = {2019},
  doi     = {10.1007/s41468-019-00038-7}
}

@article{crane2013geodesics,
  author  = {Crane, Keenan and Weischedel, Clarisse and Wardetzky, Max},
  title   = {Geodesics in Heat: A New Approach to Computing Distance Based on Heat Flow},
  journal = {ACM Transactions on Graphics},
  volume  = {32},
  number  = {5},
  articleno = {152},
  pages   = {152:1--152:11},
  year    = {2013},
  doi     = {10.1145/2516971.2516977}
}

@article{sharp2019vector,
  author  = {Sharp, Nicholas and Soliman, Yousuf and Crane, Keenan},
  title   = {The Vector Heat Method},
  journal = {ACM Transactions on Graphics},
  volume  = {38},
  number  = {3},
  articleno = {24},
  pages   = {24:1--24:19},
  year    = {2019},
  doi     = {10.1145/3243651}
}

@inproceedings{sorkine2004laplacian,
  author    = {Sorkine, Olga and Cohen-Or, Daniel and Lipman, Yaron and Alexa, Marc and R{\"o}ssl, Christian and Seidel, Hans-Peter},
  title     = {Laplacian Surface Editing},
  booktitle = {Proceedings of the 2004 Eurographics/ACM SIGGRAPH Symposium on Geometry Processing (SGP)},
  pages     = {175--184},
  year      = {2004},
  publisher = {ACM},
  doi       = {10.1145/1057432.1057456}
}

@article{bouaziz2014projective,
  author  = {Bouaziz, Sofien and Martin, Sebastian and Liu, Tiantian and Kavan, Ladislav and Pauly, Mark},
  title   = {Projective Dynamics: Fusing Constraint Projections for Fast Simulation},
  journal = {ACM Transactions on Graphics},
  volume  = {33},
  number  = {4},
  articleno = {154},
  pages   = {154:1--154:11},
  year    = {2014},
  doi     = {10.1145/2601097.2601116}
}

@article{elcott2007stable,
  author  = {Elcott, Sharif and Tong, Yiying and Kanso, Eva and Schr{\"o}der, Peter and Desbrun, Mathieu},
  title   = {Stable, Circulation-Preserving, Simplicial Fluids},
  journal = {ACM Transactions on Graphics},
  volume  = {26},
  number  = {1},
  articleno = {4},
  pages   = {4:1--4:12},
  year    = {2007},
  doi     = {10.1145/1189762.1189766}
}

@misc{desbrun2005discrete,
  author = {Desbrun, Mathieu and Hirani, Anil N. and Leok, Melvin and Marsden, Jerrold E.},
  title  = {Discrete Exterior Calculus},
  year   = {2005},
  eprint = {math/0508341},
  archivePrefix = {arXiv},
  primaryClass  = {math.DG},
  note   = {arXiv:math/0508341}
}

@book{ehrig2006fundamentals,
  author    = {Ehrig, Hartmut and Ehrig, Karsten and Prange, Ulrike and Taentzer, Gabriele},
  title     = {Fundamentals of Algebraic Graph Transformation},
  series    = {Monographs in Theoretical Computer Science (EATCS Series)},
  publisher = {Springer},
  year      = {2006},
  doi       = {10.1007/3-540-31188-2}
}
```

_Notes on residual uncertainty:_ the ARITH 2013 DOI (10.1109/ARITH.2013.9) and page range 163–172 come from search results and the Google Scholar DOI lookup, not a direct IEEE Xplore fetch (IEEE was not opened). The TOMS 2020 article number (22) is inferred from the 46(3) issue; Crossref reports pages 1–49 but not the article number explicitly. All other article numbers/pages/DOIs were read directly from dblp, Crossref, or the authors' bibtex pages.
