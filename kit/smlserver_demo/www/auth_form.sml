val target = 
  case FormVar.wrapOpt FormVar.getStringErr "target" 
    of SOME t => t
     | NONE => "/index.sml"

val _ = Page.return "Login to SMLserver.org" `
To modify the link database, you must enter your <b>email address</b> and your <b>password</b>.
<form action="/auth.sml" method=post>
<table>
 <input type=hidden name=target value="^target">
 <tr><td><b>Email address</b></td>
     <td><input type=text name=email size=20></td>
 </tr>
 <tr><td><b>Password</b></td>
     <td><input type=password name=passwd size=20>
     </td>
 </tr>
 <tr><td colspan=2 align=center>
        <input type=submit value=Login>
     </td>
 </tr>
</table>
</form>
If you're not already a member, you may register
by filling out a <a href=auth_new.sml>form</a>.`
