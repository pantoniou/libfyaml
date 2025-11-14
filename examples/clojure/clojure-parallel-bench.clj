;; Parallel map/reduce benchmark for Clojure
(require '[clj-yaml.core :as yaml])

(defn process-item [item]
  "Process a single item - add metadata"
  (if (map? item)
    (assoc item 
           :processed true 
           :thread-id (.getId (Thread/currentThread)))
    item))

(defn -main [filename n-threads]
  (let [;; Parse YAML
        start-parse (System/nanoTime)
        content (slurp filename)
        data (yaml/parse-string content)
        parse-time (/ (- (System/nanoTime) start-parse) 1e6)
        
        ;; Parallel map over sequence
        start-pmap (System/nanoTime)
        processed (if (sequential? data)
                    (doall (pmap process-item data))
                    data)
        pmap-time (/ (- (System/nanoTime) start-pmap) 1e6)
        
        ;; Emit back to YAML
        start-emit (System/nanoTime)
        output (yaml/generate-string processed)
        emit-time (/ (- (System/nanoTime) start-emit) 1e6)]
    
    ;; Print YAML output to stdout
    (println output)
    
    ;; Print timings to stderr
    (.println System/err (format "Parse:    %.2f ms" parse-time))
    (.println System/err (format "Parallel: %.2f ms (%d items)" 
                                pmap-time 
                                (if (sequential? data) (count data) 0)))
    (.println System/err (format "Emit:     %.2f ms" emit-time))
    (.println System/err (format "Total:    %.2f ms" 
                                (+ parse-time pmap-time emit-time))))
  
  (System/exit 0))

(let [filename (first *command-line-args*)
      n-threads (or (some-> (second *command-line-args*) Integer/parseInt) 8)]
  (when-not filename
    (println "Usage: clj clojure-parallel-bench.clj <yaml-file> [n-threads]")
    (System/exit 1))
  (-main filename n-threads))
