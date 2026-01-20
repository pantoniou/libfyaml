;; Parallel map/reduce benchmark for Clojure
(require '[clj-yaml.core :as yaml])

(defn process-item [item]
  "Example processing function - simulates work"
  (if (map? item)
    (assoc item :processed true :thread (.getName (Thread/currentThread)))
    item))

(defn parallel-process [data n-threads]
  "Parallel map over collection using pmap"
  (if (sequential? data)
    (let [start (System/nanoTime)
          ;; pmap automatically parallelizes
          result (doall (pmap process-item data))
          elapsed (/ (- (System/nanoTime) start) 1e6)]
      (binding [*out* *err*]
        (printf "Parallel processing (%d threads): %.2f ms\n" n-threads elapsed))
      result)
    data))

(defn -main [filename]
  (let [;; Read and parse
        start-parse (System/nanoTime)
        content (slurp filename)
        data (yaml/parse-string content)
        parse-time (/ (- (System/nanoTime) start-parse) 1e6)

        ;; Parallel processing
        processed (parallel-process data 8)

        ;; Serialize back
        start-emit (System/nanoTime)
        output (yaml/generate-string processed)
        emit-time (/ (- (System/nanoTime) start-emit) 1e6)]

    (println output)
    (binding [*out* *err*]
      (printf "Parse:   %.2f ms\n" parse-time)
      (printf "Emit:    %.2f ms\n" emit-time)
      (printf "Total:   %.2f ms\n" (+ parse-time emit-time)))))

(when-let [filename (first *command-line-args*)]
  (-main filename))
