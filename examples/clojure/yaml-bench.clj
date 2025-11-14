#!/usr/bin/env clojure

;; Simple YAML benchmark using clj-yaml
;; Run with: clojure -Sdeps '{:deps {clj-commons/clj-yaml {:mvn/version "1.0.27"}}}' yaml-bench.clj <file.yaml>

(require '[clj-yaml.core :as yaml])

(defn -main [& args]
  (when (empty? args)
    (println "Usage: clojure -Sdeps '{:deps {clj-commons/clj-yaml {:mvn/version \"1.0.27\"}}}' yaml-bench.clj <yaml-file>")
    (System/exit 1))

  (let [filename (first args)

        ;; Read YAML file
        start-read (System/nanoTime)
        content (slurp filename)
        data (yaml/parse-string content)
        read-time (/ (- (System/nanoTime) start-read) 1e6)

        ;; Dump it back
        start-write (System/nanoTime)
        output (yaml/generate-string data)
        write-time (/ (- (System/nanoTime) start-write) 1e6)]

    (println output)
    (binding [*out* *err*]
      (println (format "\nRead:  %.2f ms" read-time))
      (println (format "Write: %.2f ms" write-time))
      (println (format "Total: %.2f ms" (+ read-time write-time))))))

(apply -main *command-line-args*)
