;; project.clj (for Leiningen build tool)
  (defproject yaml-bench "0.1.0"
    :dependencies [[org.clojure/clojure "1.11.1"]
                   [clj-yaml "0.7.0"]]
    :main yaml-bench.core)

  ;; src/yaml_bench/core.clj
  (ns yaml-bench.core
    (:require [clj-yaml.core :as yaml])
    (:gen-class))

  (defn -main [& args]
    (when (empty? args)
      (println "Usage: lein run <yaml-file>")
      (System/exit 1))

    (let [filename (first args)
          start (System/nanoTime)

          ;; Read YAML file
          content (slurp filename)
          data (yaml/parse-string content)
          read-time (/ (- (System/nanoTime) start) 1e6)

          ;; Dump it back
          start2 (System/nanoTime)
          output (yaml/generate-string data)
          write-time (/ (- (System/nanoTime) start2) 1e6)]

      (println output)
      (println (format "\nRead:  %.2f ms" read-time))
      (println (format "Write: %.2f ms" write-time))
      (println (format "Total: %.2f ms" (+ read-time write-time)))))

