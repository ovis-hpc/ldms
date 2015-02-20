###
file: baler.coffee
author: Narate Taerat (narate at ogc dot us)

This is a javascript package for communicating with bhttpd and constructing
Baler Web GUI widgets.

###

define ["jquery", "baler_config", "lazy_html"], ($, bcfg, _) -> baler =
    balerd:
        addr: (()->
            host = window.location.hostname
            if (!host)
                host = 'localhost'
            port = window.location.port
            if (bcfg.bhttpd.host)
                host = bcfg.bhttpd.host
            if (bcfg.bhttpd.port)
                port = bcfg.bhttpd.port
            hostport = host
            if (port)
                hostport += ":#{port}"
            return hostport
        )()

    tkn2html : (tok) -> "<span class='baler_#{tok.tok_type}'>#{tok.text}</span>"

    msg2html : (msg) ->
        baler.tkn2html(tok) for tok in msg

    query: (param, cb) ->
        url = "http://#{baler.balerd.addr}/query"
        $.getJSON(url, param, cb)

    get_test: (param, cb) ->
        url = "http://#{baler.balerd.addr}/test"
        $.getJSON(url, param, cb)

    get_ptns : (cb) ->
        baler.query({"type": "ptn"}, cb)

    get_meta : (cb) ->
        baler.query({"type": "meta"}, cb)

    msg_cmp: (msg0, msg1) ->
        if msg0.ts == msg1.ts
            if msg0.host == msg1.host
                return 0
            if msg0.host < msg1.host
                return -1
            return 1
        if msg0.ts < msg1.ts
            return -1
        return 1

    Disp: class Disp
        domobj: undefined
        element_type: undefined
        constructor: (@element_type) ->
            @domobj = _.tag(@element_type)

    TokDisp: class TokDisp extends Disp
        tok: undefined
        constructor: (@tok) ->
            @domobj = _.span({class: "baler_#{@tok.tok_type}"}, @tok.text)

    MsgDisp: class MsgDisp extends Disp
        msg: undefined
        constructor: (@msg) ->
            @domobj = _.span(null, t.domobj for t in \
                                (new TokDisp(tok) for tok in @msg))

    MsgLstEnt: class MsgLstEnt extends Disp
        msg: undefined
        constructor: (@msg) ->
            m = new MsgDisp(@msg.msg)
            ts = _.span({class: "timestamp"}, @msg.ts)
            host = _.span({class: "host"}, @msg.host)
            @domobj = _.li({class: "MsgLstEnt"}, ts, " ", host, " ", m.domobj)

    PtnLstEnt: class PtnLstEnt extends Disp
        ptn: undefined
        constructor: (@ptn) ->
            m = new MsgDisp(@ptn.msg)
            @domobj = _.li({class: "PtnLstEnt"}, "[#{@ptn.ptn_id}]:", m.domobj)

    GrpLstEnt: class GrpLstEnt extends Disp
        subdom: undefined
        namedom: undefined
        name: undefined
        gid: undefined
        expanded: true
        toggleExpanded: () ->
            @expanded = !@expanded
            if @expanded
                @subdom.style.display = ""
            else
                @subdom.style.display = "none"

        namedom_onClick: (_this_) ->
            _this_.toggleExpanded()

        constructor: (@name, @gid) ->
            @namedom = _.div({class: "GrpLstEnt_name"}, @name)
            @subdom = _.ul()
            @domobj = _.li({class: "GrpLstEnt"}, @namedom, @subdom)

            fn = @namedom_onClick
            obj = this
            @namedom.onclick = () -> fn(obj)

        addPtnLstEnt: (p) ->
            @subdom.appendChild(p.domobj)

    # -- end GrpLstEnt class -- #


    PtnTable: class PtnTable extends Disp
        ptns: undefined
        groups: undefined
        ptns_ent: [] # ptns_dom[ptn_id] is PtnLstEnt of ptn_id
        groups_ent: [] # groups_dom[gid] is GrpLstEnt of gid

        constructor: (__ptns__, @groups) ->
            @domobj = _.ul({class: "PtnTable"})

            @ptns = []
            for p in __ptns__
                @ptns[p.ptn_id] = p
            for pid in Object.keys(@ptns)
                # ptn dom object creation
                ptn = @ptns[pid]
                p = @ptns_ent[pid] = new PtnLstEnt(ptn)
                # group dom object creation
                gid = @groups[pid]
                if not gid
                    gid = 0
                g = @groups_ent[gid]
                if not g
                    g = @groups_ent[gid] = new GrpLstEnt("#{gid}", gid)
                    @domobj.appendChild(g.domobj)
                g.addPtnLstEnt(p)

        sort: () ->
            @groups_ent.sort (a,b) ->
                if a.gid == b.gid
                    return 0
                if a.gid < b.gid
                    return -1
                return 1
            for g in @groups_ent
                if g
                    @domobj.appendChild(g.domobj)
            0
    # -- end PtnTable class -- #

    MsgTableControl: class MsgTableControl extends Disp
        dom:
            root: null
            input:
                ts0: null
                ts1: null
                host_ids: null
                ptn_ids: null
            apply_btn: null
            up_btn: null
            down_btn: null
            uup_btn: null
            ddown_btn: null

        dom_input_label_placeholder:
            ts0: ["Timestamp begin: ", "yyyy-mm-dd HH:MM:SS"]
            ts1: ["Timestamp end: ", "yyyy-mm-dd HH:MM:SS"]
            host_ids: ["Host list: ", "example: 1,2,5-10"]
            ptn_ids: ["Pattern ID list: ", "example: 1,2,5-10"]

        msgTable: undefined

        getQueryInput: () ->
            q = {}
            for k,obj of @dom.input when obj.value
                q[k] = obj.value
            return q

        on_apply_btn_click: (event) ->
            query = @getQueryInput()
            query.type = "msg"
            query.dir = if query.ts0 then "fwd" else "bwd"
            @msgTable.query(query)

        getOlder: (event, n) ->
            @msgTable.fetchOlder(n)

        constructor: (@msgTable) ->
            _this_ = this
            ul = _.ul({style: "list-style: none"})
            for k, [lbl, plc] of @dom_input_label_placeholder
                input = @dom.input[k] = _.input()
                input.placeholder = plc
                li = _.li(null, _.span(null, lbl), input)
                ul.appendChild(li)
            btns = [
                @dom.apply_btn = _.button(null, "apply"),
                @dom.uup_btn = _.button(null, "\u21c8"),
                @dom.up_btn = _.button(null, "\u21bf"),
                @dom.down_btn = _.button(null, "\u21c2"),
                @dom.ddown_btn = _.button(null, "\u21ca")
            ]

            @dom.apply_btn.onclick = (event) -> _this_.on_apply_btn_click(event)
            @dom.uup_btn.onclick = (event) -> _this_.msgTable.fetchOlder(5)
            @dom.up_btn.onclick = (event) -> _this_.msgTable.fetchOlder(1)
            @dom.down_btn.onclick = (event) -> _this_.msgTable.fetchNewer(1)
            @dom.ddown_btn.onclick = (event) -> _this_.msgTable.fetchNewer(5)

            @dom.root = _.div({class: "MsgTableControl"}, ul, btns)
            @domobj = @dom.root

    # -- end MsgTableControl class -- #


    MsgTable: class MsgTable extends Disp
        query_param:
            type: "msg"
            ts0: undefined
            ts1: undefined
            host_ids: undefined
            ptn_ids: undefined
            session_id: undefined
            n: 10
            dir: "bwd"

        tableSize: 15

        dom:
            root: null
            ul: null

        msgent_list: [] # Current list of MsgLstEnt's  .. use msg.ref as key
        msgent_first: undefined
        msgent_last: undefined

        constructor: (tableSize) ->
            if tableSize
                @tableSize = tableSize
            @dom.ul = _.ul({style: "list-style: none"})
            @domobj = @dom.root = _.div(null, @dom.ul)

        clearTable: () ->
            ul = @dom.ul
            ul.removeChild(ul.firstChild) while (ul.firstChild)
            @msgent_list = []

        initTable: (msgs) ->
            @clearTable()
            if @query_param.dir == "fwd"
                @__addFn = @addNewerEnt
            else
                @__addFn = @addOlderEnt
            for msg in msgs
                ent = new MsgLstEnt(msg)
                @__addFn(ent, msg.ref)

        addOlderEnt: (ent, ref) ->
            ent.domobj.msgref = ref
            @msgent_list[ref] = ent
            @dom.ul.insertBefore(ent.domobj, @dom.ul.firstChild)

        addNewerEnt: (ent, ref) ->
            ent.domobj.msgref = ref
            @msgent_list[ref] = ent
            @dom.ul.appendChild(ent.domobj)

        removeMsg: (msgref) ->
            ent = @msgent_list[msgref]
            delete @msgent_list[msgref]
            @dom.ul.removeChild(ent.domobj)

        removeOlderEnt: (n) ->
            if not n
                return
            for i in [1..n]
                @removeMsg(@dom.ul.firstChild.msgref)

        removeNewerEnt: (n) ->
            if not n
                return
            for i in [1..n]
                @removeMsg(@dom.ul.lastChild.msgref)

        __addFn: undefined
        __removeFn: undefined

        updateTable: (msgs) ->
            window.msgs = msgs
            old_refs = Object.keys(@msgent_list)
            if @query_param.dir == "fwd"
                @__addFn = @addNewerEnt
                @__removeFn = @removeOlderEnt
            else
                @__addFn = @addOlderEnt
                @__removeFn = @removeNewerEnt

            # For debugging #
            for elm in @dom.ul.childNodes
                elm.classList.remove("NewRow")
            # --------------#
            count = 0
            for msg in msgs
                if @msgent_list[msg.ref]
                    continue
                ent = new MsgLstEnt(msg)
                ent.domobj.classList.add("NewRow")
                _.addChildren(ent.domobj, " ##{count++}")
                @__addFn(ent, msg.ref)

            @__removeFn(count)

        query_cb: (data, textStatus, jqXHR) ->
            window.__msgtable = {data: data, textStatus: textStatus, jqXHR: jqXHR}
            if not data.session_id
                cosole.log("MsgTable query error
                    (check __msgtable_datat for debugging)")
                return -1
            if @query_param.session_id != data.session_id
                # New bhttpd session, treat as new table
                @query_param.session_id = data.session_id
                @initTable(data.msgs)
                return 0
            # Reaching here means updating the table ...
            @updateTable(data.msgs)

        query: (param) ->
            if param
                # Destroy old session_id
                if @query_param.session_id
                    url = "http://#{baler.balerd.addr}/query/destroy_session?"+
                        "session_id=#{@query_param.session_id}"
                    $.ajax(url, {})
                @query_param = param
            @query_param.n ?= @tableSize
            _this_ = this
            baler.query(@query_param, (data, textStatus, jqXHR) ->
                _this_.query_cb(data, textStatus, jqXHR)
            )

        fetchOlder: (n) ->
            @query_param.n = n
            if @query_param.dir == "fwd"
                @query_param.n += @tableSize - 1
            @query_param.dir = "bwd"
            @query()

        fetchNewer: (n) ->
            this.query_param.n = n
            if @query_param.dir == "bwd"
                @query_param.n += @tableSize - 1
            this.query_param.dir = "fwd"
            this.query()


    # -- end MsgTable class -- #

    Parent: class Parent
        constructor: (@name) ->
            console.log("parent constructor")

        say: (text) ->
            console.log("#{@name}: #{text}")

    Child: class Child extends Parent
        constructor: (@name) ->
            super(name)
            console.log("child constructor")

        say: (text) ->
            console.log("child .. #{@name}: #{text}")
