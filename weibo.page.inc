<?php
// $Id$

/**
 * 绑定一个新浪微博帐号并验证授权，获得访问令牌
 */
function weibo_page_validate() {
  global $user;

  if (!$user->uid && !user_access('weibo user login')) {
    /**
     * 未注册用户，不允许使用新浪微博帐号登录
     */
    dd_set_message(t('weibo', '不允许使用新浪微博帐号登录'), 'error');
    dd_goto('user/login');
  }
  
  if ($user->uid && $user->ext->weibo) {
    if ($count = db_query('SELECT COUNT(aid) FROM {user_ext} WHERE type = ? 
    AND uid = ?', array('weibo', $user->uid), array('return' => 'column'))) {
      if (var_get_key('weibo', 'user_max', 1) < $count) {
        dd_set_message(t('weibo', '只允许绑定 @count 个新浪微博帐号', array('@count' => $count)), 'error');
        dd_goto('user/center/weibo');
      }
    }
  }

  /**
   * 从新浪微博接口返回，使用返回参数进行验证
   */
  if ($_GET['op'] == 'validate') {
    if ($token = weibo_get_access_token()) {
      _weibo_user_validate($token);
    } else {
      dd_set_message(t('weibo', '授权未通过验证'), 'error');
      dd_goto('user');
    }
    exit;
  }
  
  if ($to_url = weibo_get_login_url()) {
    dd_goto($to_url);
  } else {
    dd_set_message(t('weibo', '新浪服务器未响应，无法验证'), 'error');
    dd_goto('user');
  }
  
}

/**
 * 让用户登录，若用户不存在，则创建新用户并登录
 * 若用户已登录，则将新浪微博帐号绑定
 * 必须先获取访问令牌
 * @param object $token
 *  访问令牌
 */
function _weibo_user_validate($token = NULL) {
  global $user;
  
  $api = new weibo(0, 0, $token);
  
  if ($o = $api->get_user_info()) {
    $weibo = new stdClass;
    $weibo->data = array();
    $weibo->data['token'] = $token;

    // 新浪用户 id 保存为 ext_type
    $weibo->ext_type = $o->id;

    // 微博帐号统一扩展分类
    $weibo->type = 'weibo';
    
    /**
     * 新浪微博昵称
     * 若为新用户，则以此为用户名，在 dida 中注册。若该用户名已存在，以数字编号递增：
     * -- dida_user
     * -- dida_user_1
     * -- dida_user_2
     * -- dida_user_3
     */
    $weibo->timestamp = time();
    
    $weibo->ext_name = $o->name;

    // 其它数据，序列化存储
    $weibo->data['info'] = $o;
    
    // 通过 微博 id 查询帐号是否已存在
    if ($info = db_query('SELECT aid, uid FROM {user_ext} WHERE type = ? AND ext_type = ?',
    array('weibo', $weibo->ext_type), array('return' => 'one'))) {

      /**
       * 更新 token 等信息
       */
      weibo_set_user_data($info->aid, $weibo->data);

      if ($user->uid) {
        if ($user->uid != $info->uid) {
          // 未与当前登录用户绑定
          dd_set_message(t('weibo', '该帐号已与其它用户绑定'), 'error');
          dd_goto('user');
        }

        // 更新
        user_clear_session($user->uid); 

      } else if ($account = user_load($info->uid)) { // 让用户登录
        if (user_login($account, 0)) {
          dd_set_message(t('weibo', '登录成功'));
        }
      } else {
        dd_set_message(t('weibo', '错误的访问'), 'error');
        dd_goto('user');
      }

      dd_goto_redirect('user/center/weibo');
      
      exit;
    }
    
    // 帐号不存在，未登录用户，注册一个新帐号
    if (empty($user->uid)) {
      $ac = new stdClass;
      
      // 检查用户名是否存在，若存在，则增加编号，产生一个新的用户名。
      $ac->name = $weibo->ext_name;
      $i = 0;
      while ($uid = db_query('SELECT uid FROM {users} WHERE name = ?',
      array($ac->name), array('return' => 'column'))) {
        $ac->name = $weibo->ext_name.'_'.$i;
        ++$i;
      }
      
      // 生成随机密码 
      $ac->pass = dd_range_str(). dd_range_str();

      // 生成一个邮箱
      $ac->mail = $weibo->ext_type . ($i ? $i : '') . '@t.sina.com.cn';

      // 设为默认帐号
      $weibo->ext_id = 1;
      
      // 写入微博帐号扩展信息
      // $ac->ext = array($weibo);

      // 用户创建成功后的状态
      $ac->status = var_get_key('weibo', 'user_status', 1) ? 1 : (var_get_key('user', 'status', 1) == 1 ? 1 : 0);
      $error = '';
      
      if ($user = user_save($ac, NULL, $error)) {
        // 写入微博帐号扩展信息，以绑定到用户
        if (!user_ext_save($user, array($weibo))) {
          dd_set_message(t('weibo', '抱歉，微博帐号绑定失败了，<a href="!url">请重试</a>', 
          array('!url' => url('weibo/validate'))));
        }

        if ($field = field_get_cache_field_key('user', 'field', 'avatar')) {
          // 将微博头像写入本地
          if ($data = file_get_contents(str_replace('/50/', '/180/', $o->profile_image_url))) {
            file_save_data($data, array(
              'uid' => $user->uid,
              'ext_id' => $field->field_id,
              'ext_id2' => $user->uid,
              'ext_type' => 'field_file',
              'filesize' => 4096,
              'filename' => $user->uid,
              'title' => $user->name . '的头像',
              'extension' => 'jpg',
              'savepath' => 'avatar',
            ), 0);
          }
        }

        dd_set_message(t('weibo', '已为您成功创建帐号：!name，随机密码：!pass，如需从!site直接登录，请记住此密码',
          array('!name' => $user->name, '!pass' => $ac->pass, '!site' => var_get_key('site_global', 'name'))));

        user_login($user, 0);

        user_clear_session($user->uid); 

        dd_goto_redirect('user/center/weibo');
      } else {
        dd_set_message(t('weibo', '无法为您创建帐号'), 'error');
        dd_goto('user/register');
      }
      
    } else if ($count = db_query('SELECT COUNT(aid) FROM {user_ext} WHERE type = ? 
    AND uid = ?', array('weibo', $user->uid), array('return' => 'column'))) {
      if (var_get_key('weibo', 'user_max', 1) < $count) {
        dd_set_message(t('weibo', '只允许绑定 @count 个新浪微博帐号', array('@count' => $count)), 'error');
        dd_goto('user/center/weibo');
      }
    }
    
    // 将该微博帐号与用户绑定
    if (user_ext_save($user, array($weibo))) {
      dd_set_message(t('weibo', '微博帐号 !name 绑定成功', array('!name' => $weibo->ext_name)));
      // 更新用户 session
      user_clear_session($user->uid); 
      dd_goto_redirect('user/center/weibo');
    } else {
      dd_set_message(t('weibo', '抱歉，微博帐号绑定失败了，<a href="!url">请重试</a>', 
      array('!url' => url('weibo/validate'))), 'error');   
    }
  }
  
  dd_set_message(t('weibo', '无法通过新浪接口验证，请重试'), 'error');
  dd_goto_redirect('user');
  
}
