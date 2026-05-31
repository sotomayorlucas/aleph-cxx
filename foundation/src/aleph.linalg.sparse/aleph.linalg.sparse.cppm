export module aleph.linalg.sparse;
export import :dense;   // DMatrix (row-major, f64)
export import :csr;     // CsrMatrix (row_ptr, col_idx, values)
export import :ldlt;    // LdltFactorization, ldlt_factorize, ldlt_solve
