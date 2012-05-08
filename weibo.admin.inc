<?php
// $Id$

/**
 * 微博设置
 */
function weibo_admin_settings(){
  
  dd_set_tabs(array(
    l(t('weibo', '微博设置'), 'admin/weibo/settings'),
    l(t('weibo', '微博用户'), 'admin/weibo/users')
  ));
  
  $form['fields']['weibo']['user_max'] = array(
    '#type' => 'textfield',
    '#title' => t('weibo', '允许绑定多个微博帐号'),
    '#default_value' => var_get_key('weibo', 'user_max', 1),
    '#required' => 1,
    '#description' => t('weibo', '默认情况下，一个用户只允许绑定一个微博帐号')
  );
   
  $form['fields']['weibo']['user_status'] = array(
    '#title' => t('weibo', '用户状态'),
    '#default_value' => var_get_key('weibo', 'user_status', 1),
    '#type' => 'radio',
    '#required' => 1,
    '#options' => array(t('weibo', '默认'), t('weibo', '激活')),
    '#description' => t('weibo', '通过微博帐号注册成功后的用户状态，默认则使用网站全局设置')
  );
   
  $form['fields']['weibo']['version'] = array(
    '#title' => t('weibo', 'API 版本'),
    '#default_value' => var_get_key('weibo', 'version', 'oauth1'),
    '#type' => 'radio',
    '#required' => 1,
    '#options' => array(
      'oauth1' => 'oauth1',
      'oauth2' => 'oauth2'
    ),
  );

  $form['fields']['weibo']['oauth_consumer_key'] = array(
    '#type' => 'textfield',
    '#title' => t('weibo', '新浪微博 API'),
    '#default_value' => var_get_key('weibo', 'oauth_consumer_key', NULL),
    '#description' => t('weibo', '你可以到 http://open.t.sina.com.cn/wiki/index.php 申请，若没有，请留空')
  );
  
  $form['fields']['weibo']['oauth_consumer_secret'] = array(
    '#type' => 'textfield',
    '#title' => t('weibo', 'API 密钥'),
    '#default_value' => var_get_key('weibo', 'oauth_consumer_secret', NULL),
    '#description' => t('weibo', '除非你自行设置了 API，否则请留空')
  );

  if ($infos = module_invoke_all('comment_info')) {
    foreach ($infos as $key => $info) {
      $form['fields']['weibo']['comment_'.$key] = array(
        '#title' => t('weibo', '在 [%name] 中启用', array('%name' => $key . $info['name'])),
        '#default_value' => var_get_key('weibo', 'comment_'.$key, 1),
        '#type' => 'radio',
        '#options' => array(t('weibo', '不启用'), t('weibo', '启用')),
      );
    }
  }
  
  return system_setting_form($form);
}

/**
 * 绑定了新浪微博帐号或通过新浪微博帐号注册的用户
 */
function weibo_admin_users() {
   
  dd_set_tabs(array(
    l(t('weibo', '微博设置'), 'admin/weibo/settings'),
    l(t('weibo', '微博用户'), 'admin/weibo/users')
  ));
  
  if ($fetch = pager_query('SELECT s.*, u.name FROM {user_ext} s INNER JOIN {users} u 
  ON s.uid = u.uid WHERE s.type = ? ORDER BY u.uid DESC', 50, 0, NULL, array('weibo'))) {
    foreach ($fetch as $o) {
      $table[] = array(
        $o->name,
        $o->ext_name,
        format_date($o->timestamp),
        dd_imp(array(
          l(t('weibo', '解除绑定'), 'admin/weibo/users', array(
            'query' => 'op=remove&uid='.$o->uid,
            'attributes' => array('class' => 'confirmajax')
          )),
          l('浏览微博', 'http://www.weibo.com/' . $o->ext_type, array('attributes' => array('target' => '_blank')))
        ))
      );
    }
    $output = theme('table', array('用户', '微博帐号', '绑定时间', '操作'), $table, array('class' => 'center_table'));
    $output .= pager_view();
  } else {
    $output = system_no_content();
  }
  
  return $output;
}
