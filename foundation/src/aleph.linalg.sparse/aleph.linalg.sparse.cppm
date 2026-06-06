export module aleph.linalg.sparse;
export import :dense;        // DMatrix (row-major, f64)
export import :csr;          // CsrMatrix (row_ptr, col_idx, values)
export import :ldlt;         // LDLT (singular-PSD), rank-k update, reconstruct
export import :ldlt_sparse;  // SparseLdlt, elimination_tree, symbolic_factor
export import :bk_ldlt;      // BkLdlt (dense Bunch-Kaufman symmetric-indefinite LDL^T)
