#!/usr/bin/clojure
;; Parallel map/reduce YAML benchmark for Clojure
;; This will demonstrate parallel processing patterns you want to compare with libfyaml

(import '[org.yaml.snakeyaml Yaml])

(defn parse-yaml [text]
  "Parse YAML using SnakeYAML directly (no external deps needed)"
  (let [yaml (Yaml.)]
    (.load yaml text)))

(defn dump-yaml [data]
  "Dump data to YAML"
  (let [yaml (Yaml.)]
    (.dump yaml data)))

(defn parallel-process [data]
  "Example: parallel map over collection"
  (if (sequential? data)
    (let [start (System/nanoTime)
          ;; Use pmap for parallel processing
          result (->> data
                     (pmap (fn [item]
                             ;; Simulate some work
                             (Thread/sleep 1)
                             (if (map? item)
                               (assoc item :processed true)
                               item)))
                     doall)
          elapsed (/ (- (System/nanoTime) start) 1e6)]
      (println (format "Parallel processing: %.2f ms" elapsed) :err)
      result)
    data))

(defn -main [& args]
  (when (empty? args)
    (println "Usage: clojure yaml-bench-parallel.clj <yaml-file>")
    (System/exit 1))

  (let [filename (first args)

        ;; Read and parse
        start (System/nanoTime)
        content (slurp filename)
        data (parse-yaml content)
        parse-time (/ (- (System/nanoTime) start) 1e6)

        ;; Parallel processing
        processed (parallel-process data)

        ;; Dump back
        start2 (System/nanoTime)
        output (dump-yaml processed)
        dump-time (/ (- (System/nanoTime) start2) 1e6)]

    (println output)
    (binding [*out* *err*]
      (println (format "Parse: %.2f ms" parse-time))
      (println (format "Dump:  %.2f ms" dump-time)))))

(apply -main *command-line-args*)
