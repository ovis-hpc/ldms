t = $("#test")[0]
window.msg_table = new baler.MsgTable()
window.msg_ctrl = new baler.MsgTableControl(window.msg_table)

t.appendChild(window.msg_ctrl.domobj)
t.appendChild(window.msg_table.domobj)

0
