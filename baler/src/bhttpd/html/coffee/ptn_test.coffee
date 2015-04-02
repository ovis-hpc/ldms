baler.get_ptns (data, textStatus, jqXHR) ->
    patterns = data.result
    baler.get_meta (data, textStatus, jqXHR) ->
        grp = []
        for p in data.map
            grp[p[0]] = p[1]
        p = new baler.PtnTable(patterns, grp, data.cluster_names)
        p.sort()
        t = $("#test0")[0]
        t.appendChild(p.domobj)

baler.get_metric_ptns (data, textStatus, jqXHR) ->
    patterns = data.result
    baler.get_metric_meta (data, textStatus, jqXHR) ->
        grp = []
        for p in data.map
            grp[p[0]] = p[1]
        p = new baler.PtnTable(patterns, grp, data.cluster_names)
        p.sort()
        t = $("#test1")[0]
        t.appendChild(p.domobj)
0
