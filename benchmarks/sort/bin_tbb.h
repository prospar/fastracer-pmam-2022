void bin( T* xs, T* xe, size_t m, T* y, size_t tally[M_MAX][M_MAX] ) {
    T tree[M_MAX-1];
    build_sample_tree( xs, xe, tree, m );

    size_t block_size;// = ((xe-xs)+m-1)/m;
    block_size = ((xe-xs)+m-1)/m;
    bindex_type* bindex = new bindex_type[xe-xs];
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0,m),
	[=,&tree](tbb::blocked_range<size_t> r, size_t thdId) { 
	  for( size_t i=r.begin(); i!=r.end(); ++i ) {
	    size_t js = i*block_size;
	    size_t je = std::min( js+block_size, size_t(xe-xs) );

	    // Map keys to bins
	    size_t freq[M_MAX];
	    map_keys_to_bins( xs+js, je-js, tree, m, bindex+js, freq );

	    // Compute where each bin starts
	    T* CHECK_AV dst[M_MAX];
	    size_t s = 0;
	    for( size_t j=0; j<m; ++j ) {
	      RecordMem(get_cur_tid(), &(dst[j]), WRITE);
	      dst[j] = y+js+s;
	      RecordMem(get_cur_tid(), &(freq[j]), WRITE);
	      s += freq[j];
	      RecordMem(get_cur_tid(), &(tally[i][j]), WRITE);
	      tally[i][j] = s;
	    }

	    // Scatter keys into their respective bins
	    for( size_t j=js; j<je; ++j ) {
	      RecordMem(get_cur_tid(), &(dst[bindex[j]]), READ);
	      *dst[bindex[j]]++ = std::move(xs[j]);
	    }
	  }
    });
    delete[] bindex;
}
