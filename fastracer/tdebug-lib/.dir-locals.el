(
 (nil . (
         (counsel-find-file-ignore-regexp . "\\(?:\\`[#.]\\)\\|\\(?:\\`.+?[#~]\\'\\)\\|.o$\\|.tar.gz$\\|TAGS\\|GPATH\\|GRTAGS\\|GTAGS\\|tramp\\|.clangd\\|.recommenders\\|PTRacer-solver")
         (lsp-file-watch-ignored . ("/\\.git$" "/\\.clangd$" "build" "built" "obj"))
         (projectile-enable-caching . t)
         (eval . (setq-local counsel-etags-project-root (projectile-project-root)
                             tags-table-files (list (projectile-project-root))))
         ))
 (c++-mode . (
              (flycheck-gcc-language-standard . "c++11")
              (flycheck-clang-language-standard . "c++11")
              (eval add-hook 'hack-local-variables-hook (lambda () (when (string= major-mode 'c++-mode) (lsp))))
              (eval . (let (
                            (clang-args (list
                                         "-std=c++11"
                                         (concat "-I" (expand-file-name "tdebug-llvm/llvm/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "tdebug-llvm/clang/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "tbb-lib/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "tdebug-lib/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "spd3-lib/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "fasttrack/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "newfasttrack/include" (projectile-project-root)))
                                         (concat "-I" (expand-file-name "new_algo/include" (projectile-project-root)))
                                         ))
                            (include-path (list
                                           (expand-file-name "tdebug-llvm/llvm/include" (projectile-project-root))
                                           (expand-file-name "tdebug-llvm/clang/include" (projectile-project-root))
                                           (expand-file-name "tbb-lib/include" (projectile-project-root))
                                           (expand-file-name "tdebug-lib/include" (projectile-project-root))
                                           (expand-file-name "spd3-lib/include" (projectile-project-root))
                                           (expand-file-name "fasttrack/include" (projectile-project-root))
                                           (expand-file-name "newfasttrack/include" (projectile-project-root))
                                           (expand-file-name "new_algo/include" (projectile-project-root))
                                           )))
                        (setq-local company-clang-arguments clang-args
                                    flycheck-clang-args clang-args
                                    flycheck-gcc-args clang-args
                                    flycheck-gcc-include-path include-path
                                    flycheck-clang-include-path include-path)
                        ))
              (lsp-clients-clangd-args . ("--compile-commands-dir=tdebug-lib"
                                          "--clang-tidy"
                                          "--pch-storage=memory"
                                          "--background-index"
                                          "--pretty"
                                          "-j=2"))
               (eval add-hook 'before-save-hook #'lsp-format-buffer nil t)
              ))
 )
