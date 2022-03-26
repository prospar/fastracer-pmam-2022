const size_t CutOff = 256;

// Polynomial multiplication C=A*B.
// A = a[0:n]
// B = b[0:n]
// C = c[0:2*n-1]
void karatsuba(T c[], const T a[], const T b[], size_t n) {
  if (n <= CutOff) {
    simple_mul(c, a, b, n);
  } else {
    size_t m = n / 2;
    temp_space<T> s(4 * (n - m));
    T * t = s.data();
    tbb::parallel_invoke(
        [=] {
          // Set c[0:n-1] = $t_0$
          // std::cout << "spawning with n = " << m << "\n";
          karatsuba(c, a, b, m);
        },
        [=] {
          // Set c[2*m:n-1] = $t_2$
          // std::cout << "spawning with n = " << n-m << "\n";
          karatsuba(c + 2 * m, a + m, b + m, n - m);
        },
        [=] {
          T * a_ = t + 2 * (n - m);
          T * b_ = a_ + (n - m);
          for (size_t j = 0; j < m; ++j) {
#if LINE_NO_PASS
            RecordMem(get_cur_tid(), (void *)&a[j], READ,28);
            RecordMem(get_cur_tid(), (void *)&a[m + j], READ,29);
            RecordMem(get_cur_tid(), &a_[j], WRITE,30);
#else
            RecordMem(get_cur_tid(), (void *)&a[j], READ);
            RecordMem(get_cur_tid(), (void *)&a[m + j], READ);
            RecordMem(get_cur_tid(), &a_[j], WRITE);
#endif
            a_[j] = a[j] + a[m + j];
            #if LINE_NO_PASS
            RecordMem(get_cur_tid(), (void *)&b[j], READ,38);
            RecordMem(get_cur_tid(), (void *)&b[m + j], READ,39);
            RecordMem(get_cur_tid(), &b_[j], WRITE,40);
            #else
            RecordMem(get_cur_tid(), (void *)&b[j], READ);
            RecordMem(get_cur_tid(), (void *)&b[m + j], READ);
            RecordMem(get_cur_tid(), &b_[j], WRITE);
            #endif
            b_[j] = b[j] + b[m + j];
          }
          if (n & 1) {
            #if LINE_NO_PASS
            RecordMem(get_cur_tid(), (void *)&a[2 * m], READ,50);
            RecordMem(get_cur_tid(), &a_[m], WRITE,51);
            #else
            RecordMem(get_cur_tid(), (void *)&a[2 * m], READ);
            RecordMem(get_cur_tid(), &a_[m], WRITE);
            #endif
            a_[m] = a[2 * m];
            #if LINE_NO_PASS
            RecordMem(get_cur_tid(), (void *)&b[2 * m], READ,58);
            RecordMem(get_cur_tid(), &b_[m], WRITE,59);
            #else
            RecordMem(get_cur_tid(), (void *)&b[2 * m], READ);
            RecordMem(get_cur_tid(), &b_[m], WRITE);
            #endif
            b_[m] = b[2 * m];
          }
          // Set t = $t_1$
          // std::cout << "spawning with n = " << n-m << "\n";
          karatsuba(t, a_, b_, n - m);
        });
    // Set t = $t_1 - t_0 - t_2$
    RecordMem(get_cur_tid(), &m, READ);
    for (size_t j = 0; j < 2 * m - 1; ++j) {
      #if LINE_NO_PASS
      RecordMem(get_cur_tid(), &c[j], READ,73);
      RecordMem(get_cur_tid(), &c[2 * m + j], READ, 74);
      RecordMem(get_cur_tid(), &c[j], READ, 75);
      RecordMem(get_cur_tid(), &c[2 * m + j], READ, 76);
      // RecordMem(get_cur_tid(), &m, READ, 77);
      // RecordMem(get_cur_tid(), &j, READ, 78);
      t[j] -= c[j] + c[2 * m + j];
      // RecordMem(get_cur_tid(), &m, READ, 80);
      #else
      RecordMem(get_cur_tid(), &c[j], READ);
      RecordMem(get_cur_tid(), &c[2 * m + j], READ);
      RecordMem(get_cur_tid(), &c[j], READ);
      RecordMem(get_cur_tid(), &c[2 * m + j], READ);
      // RecordMem(get_cur_tid(), &m, READ);
      // RecordMem(get_cur_tid(), &j, READ);
      t[j] -= c[j] + c[2 * m + j];
      // RecordMem(get_cur_tid(), &m, READ);
      #endif
    }
    // Add $(t_1 - t_0 - t_2) K$ into final product.
    #if LINE_NO_PASS
    RecordMem(get_cur_tid(), &c[2 * m - 1], WRITE, 94);
    // RecordMem(get_cur_tid(), &m, READ, 95);
    c[2 * m - 1] = 0;
    // RecordMem(get_cur_tid(), &m, READ, 97);
    #else
    RecordMem(get_cur_tid(), &c[2 * m - 1], WRITE);
    // RecordMem(get_cur_tid(), &m, READ);
    c[2 * m - 1] = 0;
    // RecordMem(get_cur_tid(), &m, READ);
    #endif
    for (size_t j = 0; j < 2 * m - 1; ++j) {
      #if LINE_NO_PASS
      RecordMem(get_cur_tid(), &c[m + j], READ, 106);
      RecordMem(get_cur_tid(), &t[j], READ, 107);
      // RecordMem(get_cur_tid(), &m, READ, 108);
      // RecordMem(get_cur_tid(), &j, READ, 109);
      RecordMem(get_cur_tid(), &c[m + j], WRITE, 110);
      #else
      RecordMem(get_cur_tid(), &c[m + j], READ);
      RecordMem(get_cur_tid(), &t[j], READ);
      // RecordMem(get_cur_tid(), &m, READ);
      // RecordMem(get_cur_tid(), &j, READ);
      RecordMem(get_cur_tid(), &c[m + j], WRITE);
      #endif
      c[m + j] += t[j];
    }
    if (n & 1)
      for (size_t j = 0; j < 2; ++j) {
        #if LINE_NO_PASS
        RecordMem(get_cur_tid(), &c[4 * m - 1 + j], READ, 123);
        RecordMem(get_cur_tid(), &t[2 * m - 1 + j], READ, 124);
        RecordMem(get_cur_tid(), &c[3 * m - 1 + j], READ, 125);
        RecordMem(get_cur_tid(), &c[3 * m - 1 + j], WRITE, 126);
        #else
        RecordMem(get_cur_tid(), &c[4 * m - 1 + j], READ);
        RecordMem(get_cur_tid(), &t[2 * m - 1 + j], READ);
        RecordMem(get_cur_tid(), &c[3 * m - 1 + j], READ);
        RecordMem(get_cur_tid(), &c[3 * m - 1 + j], WRITE);
        #endif
        c[3 * m - 1 + j] += t[2 * m - 1 + j] - c[4 * m - 1 + j];
      }
  }
}
