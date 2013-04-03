<?php
// $Id$

/**
 * @file oauth1 版本的 api 文件
 */

/**
 * 获取 http 请求所需的参数
 * @param array $params
 */
function weibo_get_request(array $params) {
  $request = new stdClass;
  
  if (!empty($params['oauth_consumer_key'])) {
    $consumer_key = $params['oauth_consumer_key'];
    $consumer_secret = $params['oauth_consumer_secret'];
  } else if ($consumer_key = var_get_key('weibo', 'oauth_consumer_key', false)) {
    $consumer_secret = var_get_key('weibo', 'oauth_consumer_secret', false);
  } else {
    $consumer_key = 401055340;
  }
  
  // 默认参数
  $defaults = array( 
    'oauth_version' => '1.0', // 版本
    'oauth_nonce' => md5(mt_rand(). $_SERVER['REQUEST_TIME']), // 随机数
    'oauth_timestamp' => time(), // 发起请求的时间戳
    'oauth_consumer_key' => $consumer_key, // app key
    'oauth_consumer_secret' => $consumer_secret, // app 密钥
    'oauth_signature_method' => 'HMAC-SHA1', // 签名方式
  );
  
  // 合并参数
  $params = array_merge($defaults, $params);
  
  if (isset($params['oauth_consumer_key'])) {
    $request->consumer->key = $params['oauth_consumer_key'];
  } else {
    dd_set_message(t('weibo', '必须提供 sina app key'), 'error');
    return false;
  }
  
  if (isset($params['oauth_consumer_secret']) && $params['oauth_consumer_secret']) {
    $request->consumer->secret = $params['oauth_consumer_secret'];
  } else {
    $request->consumer->secret = NULL;
  }
  
  unset($params['oauth_consumer_secret']);
  
  // 访问令牌
  $request->token = NULL;
  if (isset($params['oauth_access_token_key'])) {
    $request->token->key = $params['oauth_access_token_key'];
    unset($params['oauth_access_token_key']);
  }
  
  if (isset($params['oauth_access_token_secret'])) {
    $request->token->secret = $params['oauth_access_token_secret'];
    unset($params['oauth_access_token_secret']);
  }
  
  $uid = isset($params['uid']) ? $params['uid'] : $GLOBALS['user']->uid;

  // 没有指定 token，但指定了用户 id，查询该用户是否已有获取授权的微博帐号
  if (empty($request->token) && $uid > 0 && empty($params['oauth_token']) && empty($params['get_login'])) {
    if ($weibo_user = weibo_get_user_info($uid)) {
      // 指定了微博帐号存在于 user_ext 表中的 aid
      if (!empty($params['aid']) && $weibo_user[$params['aid']]) {
        $request->token = $weibo_user[$params['aid']]->data['token'];
      } else if ($info = weibo_get_user_default($uid)){
        // 未指定，则使用默认微博帐号
        $request->token = $info->data['token'];
      }
    }
  }
  
  if ($request->token->key && !isset($params['oauth_token'])) {
    $params['oauth_token'] = $request->token->key;
  }
  
  unset($params['uid'], $params['aid']);
  
  if (isset($params['http_method'])) {
    $request->http_method = $params['http_method'];
    unset($params['http_method']);
  } else {
    $request->http_method = 'POST';
  }
  
  if (isset($params['http_url'])) {
    $request->http_url = $params['http_url'];
    unset($params['http_url']);
  } else {
    return false;
  }

  // 有分界符，则表示需要上传文件
  if (!empty($params['boundary'])) {
    $boundary = $params['boundary'];
    $_values = $params; // 保存一份原始数据
    unset($params['boundary']);
  } else {
    $boundary = false;
  }

  // 参数中不允许包含签名
  if (isset($params['oauth_signature'])) {
    unset($params['oauth_signature']);
  }

  $keys = weibo_urlencode_rfc3986(array_keys($params));
  $values = weibo_urlencode_rfc3986(array_values($params));
  $params = array_combine($keys, $values);
  
  uksort($params, 'strcmp');

  // 签名数据
  $sign_params = array();

  // 提交数据
  $urls = array();

  // 文件数据
  $files = array();

  /**
   * 新浪微博接口未完全遵守 oauth 标准
   * 除文件上传外，所有数据均需要 url 编码
   * 文件上传时，使用 multipart/form-data 模式，不需要 url 编码
   * 签名数据均需要编码
   */
  foreach ($params as $key => $value ) {
    if (is_array($value)) {
      natsort($value);
      foreach ($value as $v2) {
        $sign_params[] = $key . '=' . $v2;
        $urls[$key] = $key . '[]=' . $v2;
      }
    } else if (!in_array($key, array('file', 'pic', 'image'))) {
      $sign_params[] = $key . '=' . $value;
      $urls[$key] = $key . '=' . $value;
    } else {
      // 文件不参与签名
      $files[$key] = $_values[$key];
    }
  }
  
  $sign_params = array($request->http_method, weibo_urlencode_rfc3986($request->http_url), weibo_urlencode_rfc3986(implode('&', $sign_params)));

  // 待签名基本字符串
  $request->base_string = implode('&', $sign_params);
  
  if ($request->consumer->secret) {
  
    $key_parts = array(
      $request->consumer->secret,
      $request->token ? $request->token->secret : ""
    );
    
    $key_parts = weibo_urlencode_rfc3986($key_parts);
    $key = implode('&', $key_parts);
    
    $request->sign = base64_encode(hash_hmac('sha1', $request->base_string, $key, true));
    
  } else {
    // 远程获取签名
    $request->sign = weibo_http_get_sign(
      $request->consumer->key,
      $request->base_string,
      ($request->token ? $request->token->secret : "")
    );
  }
  
  if (!$request->sign) {
    dd_set_message(t('weibo', '无法取得签名'), 'error');
    return false;
  }
  
  $urls['oauth_signature'] = 'oauth_signature=' . weibo_urlencode_rfc3986($request->sign);

  $request->field_data = $urls;

  // 文本
  if (empty($boundary)) {
    $request->to_data = implode('&', $urls);
  } else if (!empty($files)) {  // 文件
    
    // 签名
    $_values['oauth_signature'] = $request->sign;

    // 分界符
    $MPboundary = '--'.$boundary;
    $endMPboundary = $MPboundary . '--';

    $request->to_data = '';
    
    // 签名以及签名中的数据
    foreach ($urls as $key => $value) {
      $request->to_data .= $MPboundary . "\r\n";
      $request->to_data .= 'Content-Disposition: form-data; name="'  . $key . "\"\r\n\r\n";
      
      // multipart/form-data 方式提交的数据不需要 urlencode 编码
      $request->to_data .= $_values[$key]. "\r\n";
    }

    // 待上传的文件
    foreach ($files as $key => $value) {
      $request->to_data .= $MPboundary . "\r\n";
      $request->to_data .= 'Content-Disposition: form-data; name="' . $key . '"; filename="' . basename($value) . '"'. "\r\n";
      $request->to_data .= 'Content-Type: '. file_get_mimetype(end(explode('.', $value))) . "\r\n\r\n";
      $request->to_data .= file_get_contents($value) . "\r\n";   
    }

    $request->to_data .=  $endMPboundary;
  } else {
    return false;
  }

  return $request;
}

/**
 * 远程获取签名，若自行申请 weibo app key，不需要远程获取
 * @param int $consumer_key
 * @param string $base_string
 * @param string $token_secret
 */
function weibo_http_get_sign($consumer_key, $base_string, $token_secret = NULL) {
  $headers = array(
    'User-Agent' => 'Sae T OAuth v0.2.0-beta2',
    'Content-Type' => 'application/x-www-form-urlencoded; charset=utf-8'
  );
  
  $data = 'consumer_key=' . weibo_urlencode_rfc3986($consumer_key). '&';
  $data .= 'base_string=' . weibo_urlencode_rfc3986($base_string);
  
  if ($token_secret) {
    $data .= '&token_secret=' . weibo_urlencode_rfc3986($token_secret);
  }
  
  $data .= '&base_url=' . weibo_urlencode_rfc3986($GLOBALS['base_url']);
  
  $result = dd_http_request('http://www.didaah.org/weibo_sign.php?op=get_sign', $data, 'POST', $headers);
  
  if ($result->code == 200 && $result->data && substr($result->data, 0, 2) == 'ok') {
    return substr($result->data, 2);
  }
}

/**
 * 获取授权令牌，让用户使用此链接跳转到新浪进行授权
 * @param array $values
 * @return string|bool
 */
function weibo_get_login_url(array $values = array()) {
  if (!isset($values['http_url'])) {
    // 请求未授权令牌 url
    $values['http_url'] = 'http://api.t.sina.com.cn/oauth/request_token';
  }
  
  if (isset($_GET['oauth_callback'])) {
    $oauth_callback = $_GET['oauth_callback'];
  } else if (isset($values['oauth_callback'])) {
    $oauth_callback = $values['oauth_callback'];
    unset($values['oauth_callback']);
  } else {
    
    if (isset($_REQUEST['redirect'])) {
      $query = 'op=validate&redirect=' . $_REQUEST['redirect'];
    } else {
      $query = 'op=validate';
    }
    
    $oauth_callback = url('weibo/validate', array('query' => $query, 'absolute' => true));
  }

  $values['get_login'] = true;

  $result = weibo_http_request($values['http_url'], $values);
  
  if ($result->data && strpos($result->data, 'oauth_token_secret') !== false) {
    $token = weibo_parse_parameters($result->data);
    if ($token['oauth_token_secret']) {
      $tok = new stdClass;
      $tok->key = $token['oauth_token'];
      $tok->secret = $token['oauth_token_secret'];
      
      $_SESSION['sina_t_oauth_token'] = $tok;
      
      $url = 'http://api.t.sina.com.cn/oauth/authenticate';
      $url .= '?oauth_token=' . $token['oauth_token'];
      $url .= '&oauth_callback=' . urlencode($oauth_callback);
      return $url;
    }
  }
  
  // 未能成功获取授权令牌
  
  return false;
}

/**
 * 获取访问令牌，用户已授权，根据授权令牌获得访问令牌
 * @param array $values
 * @param array|bool
 */
function weibo_get_access_token(array $values = array()) {
  if (!isset($values['http_url'])) {
    $values['http_url'] = 'http://api.t.sina.com.cn/oauth/access_token';
  }
  
  if (!isset($values['oauth_verifier']) && isset($_GET['oauth_verifier'])) {
    $values['oauth_verifier'] = $_GET['oauth_verifier'];
  }
  
  if (!isset($values['oauth_token']) && isset($_GET['oauth_token'])) {
    $values['oauth_token'] = $_GET['oauth_token'];
  }
 
  $result = weibo_http_request($values['http_url'], $values);
  
  if ($result->data) {
    $token = weibo_parse_parameters($result->data);
    if ($token['oauth_token_secret']) {
      unset($_SESSION['sina_t_oauth_token']);
      $tok = new stdClass;

      // 记录 app key 
      $tok->oauth_consumer_key = var_get_key('weibo', 'oauth_consumer_key', false);
      $tok->oauth_consumer_secret = var_get_key('weibo', 'oauth_consumer_secret', false);

      // 访问令牌
      $tok->key = $token['oauth_token'];
      $tok->secret = $token['oauth_token_secret'];
      $tok->verifier = $values['oauth_verifier'];
      
      return $tok;
    }
  }
}

/**
 * 执行一个 http 请求
 */
function weibo_http_request($http_url, array $values = array(), array $headers = array(), $method = 'POST') {
  if (!isset($headers['User-Agent'])) {
    $headers['User-Agent'] = 'Sae T OAuth v0.2.0-beta2';
  }
  
  if (!isset($headers['Content-Type'])) {
    $headers['Content-Type'] = 'application/x-www-form-urlencoded; charset=utf-8';
  }
  
  $values['http_url'] = $http_url;
  $values['http_method'] = $method;
  
  if (!$request = weibo_get_request($values)) {
    return false;
  }

  // @TODO 新浪 api 针对单个 ip 有严格的访问限制，可自定义一个函数用来分发请求。暂时的处理办法
  if (!function_exists('__weibo_extend_http_request')) {
    return dd_http_request($request->http_url, $request->to_data, $method, $headers);
  } else {
    $key = '';
    return __weibo_extend_http_request($request->http_url, $request->to_data, $method, $headers, $key);
  }
}

/**
 * 微博操作类 
 */
class weibo {

  // 用户 uid
  var $uid = 0;

  // 微博帐号 id，对应 user_ext 表
  var $aid = 0;

  // 微博 uid
  var $weibo_uid = 0;

  // 微博帐号昵称
  var $weibo_name = '';

  // 微博访问令牌
  var $token;

  var $error;

  protected $_values = array(); 

  /**
   * 构造函数 
   * @param int $uid 
   * @param int $aid 
   * @param int $token 
   * @access public
   * @return bool
   */
  public function __construct($uid = 0, $aid = 0, $token = 0) {
    if (!empty($token) && !empty($token->key)) {
      $this->token = $token;
    } else {
      if (!empty($aid)) {
        $info = user_ext_rows_load($aid);
      } else if (!empty($uid)) {
        $info = weibo_get_user_default($uid);
      } else {
        $is_access = true;
      }
      
      if (!empty($info)) {
        $this->token = $info->data['token'];
        $this->uid = $info->uid;
        $this->weibo_uid = $info->ext_type;
        $this->weibo_name = $info->ext_name;
        $this->aid = $info->aid;
      }
    }

    $this->_values = array(
      'oauth_access_token_key' => $this->token->key,
      'oauth_access_token_secret' => $this->token->secret
    );

    if ($this->token->oauth_consumer_key) {
      $this->_values['oauth_consumer_key'] = $this->token->oauth_consumer_key;
    }
    
    if ($this->token->oauth_consumer_secret) {
      $this->_values['oauth_consumer_secret'] = $this->token->oauth_consumer_secret;
    }
    
    if (!isset($this->token->verifier)) {
      $this->_values['oauth_verifier'] = $this->token->verifier;
    }

    if (empty($this->token->key)) {
      if (empty($is_access)) {
        dd_set_message(t('weibo', '新浪微博授权已过期，请<a href="!url">重新获取授权</a>', array('!url' => url('weibo/validate'))), 'error');
      }
      return false;
    } else {
      return true;
    }
  }
   
  /**
   * 执行一个 http 请求
   */
  public function http($http_url, array $values = array(), array $headers = array(), $method = 'POST') {
    if (!isset($headers['User-Agent'])) {
      $headers['User-Agent'] = 'Sae T OAuth v0.2.0-beta2';
    }
    
    if (!isset($headers['Content-Type'])) {
      $headers['Content-Type'] = 'application/x-www-form-urlencoded; charset=utf-8';
    }
    
    $values += $this->_values;
    $values['http_url'] = $http_url;
    $values['http_method'] = $method;

    if (!$request = weibo_get_request($values)) {
      return false;
    }

    // @TODO 新浪 api 针对单个 ip 有严格的访问限制，可自定义一个函数用来分发请求。暂时的处理办法
    if (!function_exists('__weibo_extend_http_request')) {
      $result = dd_http_request($request->http_url, $request->to_data, $method, $headers);
    } else {
      $result = __weibo_extend_http_request($request->http_url, $request->to_data, $method, $headers, $host);
    }
    
    if ($result->code == 200) {
      $data = json_decode($result->data);
      if (is_object($data) && !empty($data->error_code)) {

        if (strpos($data->error, '40022') !== false) {
          dd_log('weibo_appkey', $this->token->uid);
        }

        $this->error = $data;
        $this->error->client_host = $host;
      } else {
        return $data;
      }
    }
  }

  /**
   * 根据当前 token 获取用户基本信息
   * @link http://open.weibo.com/wiki/Account/verify_credentials
   * @access public
   * @return object
   */
  public function get_user_info() {
    return $this->http('http://api.t.sina.com.cn/account/verify_credentials.json');
  }
    
  /**
   * 根据用户名、id 获取用户基本信息
   * @link http://open.weibo.com/wiki/Users/show
   * @param array $array
   *  :id、user_id、screen_name 必有其一
   * @access public
   * @return object
   */
  public function get_user_show($array) {
    return $this->http('http://api.t.sina.com.cn/users/show.json', $array);
  }

  /**
   * 发布一条微博 
   * @param string $text 
   *  微博内容
   * @param string $url
   *  附加到微博末尾的 url
   * @param string $filepath
   *  图片路径，发布为一条图片微博
   * @access public
   * @return object
   */
  public function set_tweet($text, $url = NULL, $filepath = NULL) {
    if (!empty($filepath)) {
      return $this->set_upload($filepath, $text, $url);
    }

    $values = array();
    $values['status'] = weibo_text_strlen($text, $url);
     
    if ($json = $this->http('http://api.t.sina.com.cn/statuses/update.json', $values)) {
      // 更新本地信息
      // @TODO 接口返回的微博数量，似乎没有将本条计算在内
      $json->user->statuses_count += 1;
      weibo_set_user_data($this->aid, array('info' => $json->user));
      return $json;
    }
  }

  /**
   * 发布一条图片微博 
   * @param string $filepath 
   *  图片地址
   * @param string $text 
   *  微博内容
   * @access public
   * @return object
   */
  public function set_upload($filepath, $text = NULL, $url = NULL) {
    
    $values = array();
    
    if ($text = weibo_text_strlen($text, $url)) {
      $values['status'] = $text;
    }
    
    $values['pic'] = $filepath;
    
    $values['source'] = var_get_key('weibo', 'oauth_consumer_key');
    
    $values['boundary'] = uniqid('------------------');

    if ($json = $this->http(
      'http://api.t.sina.com.cn/statuses/upload.json',
      $values,
      array('Content-Type' => 'multipart/form-data; boundary=' . $values['boundary'])
    )) {
      // 更新本地信息
      // @TODO 接口返回的微博数量，似乎没有将本条计算在内
      $json->user->statuses_count += 1;
      weibo_set_user_data($this->aid, array('info' => $json->user));
      return $json;
    }  
  }

  /**
   * 获取用户的微博列表，不指定则读取当前授权用户
   * @link http://open.weibo.com/wiki/Statuses/user_timeline
   * @link http://open.weibo.com/wiki/2/Statuses/user_timeline
   * @param int $uid 
   *  微博 uid，指定则按 uid 读取
   * @param int $screen_name 
   *  微博显示用户名，指定则按用户名读取
   * @param array $array
   *  其它数据
   * @access public
   * @return array
   */
  public function get_timeline($uid = 0, $screen_name = 0, array $values = array()) {

    if (!empty($screen_name)) $values['screen_name'] = $screen_name;

    if (!empty($uid)) $values['user_id'] = $uid;
    
    return $this->http('http://api.t.sina.com.cn/statuses/user_timeline.json', $values);
  }

  /**
   * 获取一组微博的评论和转发数 
   * @param string $ids 
   * @access public
   * @return array
   */
  public function get_counts($ids) {

    $values = array('ids' => $ids);

    return $this->http('http://api.t.sina.com.cn/statuses/counts.json', $values);
  }

  /**
   * 对微博或回复，进行评论
   * @link http://open.weibo.com/wiki/Statuses/comment 
   * @param string $text
   *  评论内容，不超过 140 字
   * @param int $id 
   *  微博 id
   * @param int $cid 
   *  回复 id
   * @param array $array 
   *  其它参数
   * @access public
   * @return object
   */
  public function set_comment($text, $id, $cid = 0, $array = array('without_mention' => 1, 'comment_ori' => 1)) {
    $values = array();
    
    if (!empty($array)) {
      $values += $array;
    }

    $values['comment'] = weibo_text_strlen($text);

    $values['id'] = $id;

    if (!empty($cid)) $values['cd'] = $cid;
 
    return $this->http('http://api.t.sina.com.cn/statuses/comment.json', $values);
  }

  /**
   * 根据微博 id 获取微博信息
   * @param int $id 
   *  指定微博 id
   * @access public
   * @return object
   */
  public function get_text($id) {
    $values = array('id' => $id);

    return $this->http('http://api.t.sina.com.cn/statuses/show/:id.json', $values);
  }
  
  /**
   * 获取表情 
   * @param string $type 
   *  表情类别，"face":普通表情，"ani"：魔法表情，"cartoon"：动漫表情 
   * @param string $lang 
   *  语言类别，"cnname"简体，"twname"繁体 
   * @access public
   * @return array
   */
  public function get_emotions($type = 'face', $lang = 'cnname') {
    $values = array();
    $values['type'] = $type;
    $values['language'] = $lang;

    return $this->http('http://api.t.sina.com.cn/emotions.json', $values);
  }

  /**
   * 获取当前用户发出的评论
   * @link http://open.weibo.com/wiki/Statuses/comments_by_me
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * $param array $options
   * @access public
   * @return array
   */
  public function get_comment_by_me($page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['count'] = $page_count;
    $values['page'] = $page_size;

    $values += $options;

    return $this->http('http://api.t.sina.com.cn/statuses/comments_by_me.json', $values);
  }

  /**
   * 根据微博 id 获取评论 
   * @param int $id 
   *  指定微博 id
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * @access public
   * @return array
   */
  public function get_comment($id, $page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['id'] = $id;
    $values['count'] = $page_count;
    $values['page'] = $page_size;

    $values += $options;

    return $this->http('http://api.t.sina.com.cn/statuses/comments.json', $values);
  }

  /**
   * 根据微博 id 获取转发 
   * @param int $id 
   *  指定微博 id
   * @param int $page_size 
   *  页码
   * @param int $page_count 
   *  每页评论数量，最大 200
   * @access public
   * @return array
   */
  public function get_repost($id, $page_size = 1, $page_count = 20, $options = array()) {
    $values = array();
    $values['id'] = $id;
    $values['count'] = $page_count;
    $values['page'] = $page_size;
    
    $values += $options;

    return $this->http('http://api.t.sina.com.cn/statuses/repost_timeline.json', $values);
  }

  /**
   * @link http://open.weibo.com/wiki/Statuses/followers
   * 根据用户 uid 获取粉丝
   * @param int $uid 
   *  用户 uid
   * @param int $page_size 
   *  页码，第一页为 -1
   * @param int $page_count 
   *  每页数量，最大 200
   * @access public
   * @return array
   */
  public function get_follower($id, $page_size = -1, $page_count = 200) {
    $values = array();
    $values['user_id'] = $id;
    $values['count'] = $page_count;
    $values['cursor'] = $page_size;
    
    return $this->http('http://api.t.sina.com.cn/statuses/followers.json', $values);
  }


  /**
   * 获取热门话题 
   * @link http://open.weibo.com/wiki/Trends/daily
   * @link http://open.weibo.com/wiki/Trends/weekly
   * @param bool $weekly 
   *  获取当周，默认为当天
   * @param int $base_app 
   *  是否限制在本应用之内
   * @access public
   * @return array
   */
  public function get_hot_trends($weekly = false, $base_app = 0) {
    $values = array();

    $values['base_app'] = $base_app;

    if ($weekly) {
      $url = 'http://api.t.sina.com.cn/trends/weekly.json';
    } else {
      $url = 'http://api.t.sina.com.cn/trends/daily.json';
    }

    return $this->http($url, $values);
  }
 
  /**
   * 获取访问次数剩余量
   * @access public
   * @return array
   */
  public function get_limit_status() {
    $url = 'http://api.t.sina.com.cn/account/rate_limit_status.json';

    return $this->http($url, $values);
  }

  /**
   * oauth1 token 换 oauth2
   * @access public
   * @return array
   */
  public function get_oauth2_token() {

    $url = 'https://api.weibo.com/oauth2/get_oauth2_token';

    return $this->http($url);
  }
}
