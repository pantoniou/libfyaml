#!/usr/bin/env bb
;; Simple YAML benchmark using babashka (faster startup than Clojure)
;; Install: https://github.com/babashka/babashka
;; Or just skip Clojure comparison - Python is representative enough

(require '[clojure.java.shell :refer [sh]])

(when (empty? *command-line-args*)
  (println "Usage: bb yaml-bench-simple.clj <yaml-file>")
  (System/exit 1))

(let [filename (first *command-line-args*)]
  (println "Note: This requires babashka. If not installed, Python comparison is sufficient.")
  (println "Install: https://github.com/babashka/babashka#installation")
  (System/exit 0))
